/* SPDX-License-Identifier: Apache-2.0 */
/*
 * MQTT-Client, siehe mqtt_task.h. Ablauf je Verbindung: CONNECT mit Last Will
 * <prefix>/online = false, auf CONNACK warten (10 s), online = true, Befehle
 * abonnieren, dann alle 100 ms Zustaende nach der Versandregel (lib/api/mqtt.c)
 * und Ereignisse senden. Abbruch oder Fehlversuch: neu verbinden mit
 * wachsender Wartezeit (SOLL-MQTT-03).
 *
 * Ein Ausfall des Brokers beruehrt die Ladelogik nicht (SOLL-MQTT-01): Die
 * Sperre der Ladelogik wird nur zum Erzeugen der Nutzdaten gehalten, nie
 * waehrend eines Sendevorgangs. Blockiert ein Senden (Broker antwortet nicht,
 * TCP-Puffer voll), haengt nur dieser Thread, bis TCP aufgibt.
 *
 * Nur gebaut und in der PC-Simulation geprueft, auf Hardware nicht ausgefuehrt.
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <wallfree/api/events.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/api/meter_json.h>
#include <wallfree/evse/param.h>
#include "evse_task.h"
#include "http_api.h"
#include "meter_task.h"
#include "mqtt_task.h"
#include "netcfg.h"

LOG_MODULE_REGISTER(mqtt, LOG_LEVEL_INF);

#define MQ_STACK          2048
#define MQ_PRIO           K_LOWEST_APPLICATION_THREAD_PRIO
#define MQ_TICK_MS        100
#define MQ_KEEPALIVE_S    60
#define MQ_CONNACK_MS     10000
#define MQ_BACKOFF_MIN_MS 1000
#define MQ_BACKOFF_MAX_MS 60000
#define MQ_TOPIC_MAX      (WF_MQTT_PREFIX_MAX + 32)

static struct mqtt_client client;
/* CONNECT ist das groesste ausgehende Paket ohne Nutzdaten: Client-ID,
 * Will-Topic und -Nachricht, Benutzer, Passwort (zusammen hoechstens ~240 Byte) */
static uint8_t tx_buf[256];
static uint8_t rx_buf[128];      /* eingehender Kopf mit Topic */
/* Alles laeuft in diesem Thread nacheinander: Die Puffer dienen beim Senden
 * (Nutzdaten, Topic), beim Empfang eines Befehls (Nutzdaten, Topic) und beim
 * Verbinden (Will-Topic, nur waehrend mqtt_connect gelesen). */
static char pay[448];
static char topic[MQ_TOPIC_MAX];
BUILD_ASSERT(sizeof(pay) >= WF_MQ_PAYLOAD_MAX);
static struct net_sockaddr_in broker;
static struct mqtt_utf8 user, password, will_msg;
static struct mqtt_topic will_topic;

static struct wf_sysconf cfg;    /* Momentaufnahme fuer diese Verbindung */
static char prefix[WF_MQTT_PREFIX_MAX + 1];
static atomic_t generation;      /* je Neukonfiguration +1 */
static K_SEM_DEFINE(wake, 0, 1);
static bool connack;
static int connack_rc;

static struct wf_mq_status st;   /* unter st_mtx */
static K_MUTEX_DEFINE(st_mtx);

/* ------------------------------------------------ Befehle (SOLL-MQTT-20/21) */

static const char *cmd_external_current(void *ctx, const char *payload, size_t len)
{
	ARG_UNUSED(ctx);
	int32_t ma;

	if (api_parse_current(payload, len, &ma) != 0) {
		return "erwartet {\"current\": mA} oder mA als Zahl";
	}
	/* wie POST /evse/external_current: Slot external, nur RAM (SOLL-NVM-03) */
	int err = evse_param_set_by_name(evse_acquire(), "external_current", ma, k_uptime_get());

	evse_release();
	if (err != EVSE_PARAM_OK) {
		return evse_param_strerror(err);
	}
	http_api_notify();
	return NULL;
}

/* Weitere Befehle: Eintrag ergaenzen (nur Laufzeitwerte, SOLL-MQTT-23) */
static const struct wf_mq_cmd cmds[] = {
	{"evse/external_current_update", cmd_external_current},
};

/* ------------------------------------------------ Zustaende (SOLL-MQTT-10/11) */

enum {
	T_STATE = 0,
	T_EXTERNAL,
	T_LIMITS,
	T_METER_IDS,
	T_METER_VALUES,
	T_METER_STATE,
	T_COUNT,
};
static const char *const names[T_COUNT] = {
	"evse/state",         "evse/external_current", "evse/limits",
	"meters/0/value_ids", "meters/0/values",       "meters/0/state",
};
static struct wf_mq_topic topics[T_COUNT];
static uint32_t ev_seq = 1;
static uint32_t meter_cycles;

static void note_error(int32_t err)
{
	k_mutex_lock(&st_mtx, K_FOREVER);
	st.last_error = err;
	k_mutex_unlock(&st_mtx);
}

static void set_conn(uint8_t conn)
{
	int64_t now = k_uptime_get();

	k_mutex_lock(&st_mtx, K_FOREVER);
	if (conn == WF_MQ_CONNECTED) {
		st.start_ms = now;
		st.last_error = 0;
	} else if (st.conn == WF_MQ_CONNECTED) {
		st.end_ms = now;
	}
	st.conn = conn;
	k_mutex_unlock(&st_mtx);
}

static bool publish(const char *name, const char *data, size_t len, bool retain)
{
	struct mqtt_publish_param p = {0};
	int n = wf_mq_topic_name(topic, sizeof(topic), prefix, name);
	int r = -ENOMEM;

	if (n > 0) {
		p.message.topic.topic.utf8 = (const uint8_t *)topic;
		p.message.topic.topic.size = (uint32_t)n;
		p.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
		p.message.payload.data = (uint8_t *)data;
		p.message.payload.len = len;
		p.retain_flag = retain ? 1 : 0;
		r = mqtt_publish(&client, &p);
	}
	k_mutex_lock(&st_mtx, K_FOREVER);
	if (r == 0) {
		st.published++;
	} else {
		st.publish_errors++;
	}
	k_mutex_unlock(&st_mtx);
	return r == 0;
}

static void render(int t, struct wf_jbuf *b)
{
	struct meter_values mv;

	wf_jb_init(b, pay, sizeof(pay));
	if (t <= T_LIMITS) {
		struct evse *e = evse_acquire();

		if (t == T_STATE) {
			api_json_state(e, b);
		} else if (t == T_EXTERNAL) {
			api_json_external_current(e, b);
		} else {
			api_json_limits(e, b);
		}
		evse_release();
		return;
	}
	if (t == T_METER_IDS) {
		wf_meter_value_ids_json(meter_task_type(), b);
		return;
	}
	(void)meter_task_get(&mv);
	if (t == T_METER_VALUES) {
		wf_meter_values_json(&mv, b);
	} else {
		wf_meter_state_json(&mv, b);
	}
}

static void publish_states(void)
{
	int64_t now = k_uptime_get();
	uint32_t interval = (uint32_t)cfg.mqtt_interval * 1000U;
	int last = meter_task_type() != METER_NONE ? T_COUNT : T_METER_IDS;

	if (last == T_COUNT) {
		struct meter_values mv;
		uint32_t n = meter_task_get(&mv);

		if (n != meter_cycles) {
			meter_cycles = n;
			wf_mq_touch(&topics[T_METER_VALUES]);
			wf_mq_touch(&topics[T_METER_STATE]);
		}
	}
	for (int t = 0; t < last; t++) {
		struct wf_jbuf b;

		/* Kennungen nur beim Verbinden; Zaehlerwerte nach Zyklus, ohne Vergleich */
		if (t >= T_METER_IDS && !wf_mq_due(&topics[t], now, interval)) {
			continue;
		}
		render(t, &b);
		if (b.overflow) {
			continue;
		}
		uint32_t h = wf_mq_hash(pay, b.len);

		if (t < T_METER_IDS) {
			wf_mq_update(&topics[t], h);
		}
		if (wf_mq_due(&topics[t], now, interval) && publish(names[t], pay, b.len, true)) {
			wf_mq_sent(&topics[t], h, now);
		}
	}
}

/* SOLL-MQTT-11: Ereignisse sofort, nicht retained */
static void publish_events(void)
{
	struct wf_event ev;

	while (evse_task_event_get(&ev_seq, &ev)) {
		struct wf_jbuf b;

		wf_jb_init(&b, pay, sizeof(pay));
		wf_ev_json(&ev, &b);
		if (!b.overflow) {
			(void)publish("evse/event", pay, b.len, false);
		}
	}
}

/* ------------------------------------------------ Empfang */

static void on_publish(struct mqtt_client *c, const struct mqtt_publish_param *p)
{
	size_t len = p->message.payload.len;
	size_t tl = MIN(p->message.topic.topic.size, sizeof(topic) - 1);
	const char *err = NULL;
	bool read_ok = true;

	/* Topic liegt im Empfangspuffer; vor dem Lesen der Nutzdaten sichern */
	memcpy(topic, p->message.topic.topic.utf8, tl);
	topic[tl] = '\0'; /* das Log kopiert Texte bis zum Nullbyte */
	for (size_t rest = len; rest > 0;) {
		size_t n = MIN(rest, (size_t)WF_MQ_PAYLOAD_MAX);

		if (mqtt_readall_publish_payload(c, (uint8_t *)pay, n) != 0) {
			read_ok = false;
			break;
		}
		rest -= n;
	}
	if (!read_ok) {
		return; /* Verbindung gestoert, mqtt_input meldet den Abbruch */
	}
	enum wf_mq_in r = wf_mq_dispatch(cmds, ARRAY_SIZE(cmds), prefix, topic, tl,
					 len <= WF_MQ_PAYLOAD_MAX ? pay : NULL, len,
					 p->retain_flag != 0, NULL, &err);

	k_mutex_lock(&st_mtx, K_FOREVER);
	wf_mq_note_command(&st, r, err);
	k_mutex_unlock(&st_mtx);
	if (r != WF_MQ_IN_OK) {
		LOG_WRN("Befehl %s: %s", topic, err);
	}
}

static void evt_cb(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connack = true;
		connack_rc = evt->result;
		break;
	case MQTT_EVT_PUBLISH:
		on_publish(c, &evt->param.publish);
		break;
	default:
		break;
	}
}

/* ------------------------------------------------ Verbindung */

static bool reconfigured(atomic_val_t gen)
{
	return atomic_get(&generation) != gen;
}

static int sock_fd(void)
{
	return client.transport.tcp.sock;
}

/* auf Daten warten und verarbeiten. @return 0, -EAGAIN (nichts) oder Fehler */
static int poll_input(int timeout_ms)
{
	struct zsock_pollfd fds = {.fd = sock_fd(), .events = ZSOCK_POLLIN};
	int r = zsock_poll(&fds, 1, timeout_ms);

	if (r < 0) {
		return -errno;
	}
	if (r == 0) {
		return -EAGAIN;
	}
	if (fds.revents & ZSOCK_POLLIN) {
		r = mqtt_input(&client);
		if (r != 0) {
			return r;
		}
	}
	if (fds.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
		return -ECONNRESET;
	}
	return 0;
}

static void client_setup(void)
{
	const char *host = net_hostname_get();

	mqtt_client_init(&client);
	broker.sin_family = NET_AF_INET;
	broker.sin_port = net_htons(cfg.mqtt_port);
	broker.sin_addr.s_addr = net_htonl(cfg.mqtt_broker);
	client.broker = &broker;
	client.evt_cb = evt_cb;
	client.client_id.utf8 = (const uint8_t *)host;
	client.client_id.size = strlen(host);
	if (cfg.mqtt_user[0] != '\0') {
		user.utf8 = (const uint8_t *)cfg.mqtt_user;
		user.size = strlen(cfg.mqtt_user);
		client.user_name = &user;
		password.utf8 = (const uint8_t *)cfg.mqtt_password;
		password.size = strlen(cfg.mqtt_password);
		client.password = cfg.mqtt_password[0] != '\0' ? &password : NULL;
	}
	/* SOLL-MQTT-04: Last Will online = false, retained */
	(void)wf_mq_topic_name(topic, sizeof(topic), prefix, "online");
	will_topic.topic.utf8 = (const uint8_t *)topic;
	will_topic.topic.size = strlen(topic);
	will_topic.qos = MQTT_QOS_0_AT_MOST_ONCE;
	will_msg.utf8 = (const uint8_t *)"false";
	will_msg.size = 5;
	client.will_topic = &will_topic;
	client.will_message = &will_msg;
	client.will_retain = 1;
	client.clean_session = 1;
	client.keepalive = MQ_KEEPALIVE_S;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);
	client.transport.type = MQTT_TRANSPORT_NON_SECURE;
}

/* @return 0 verbunden, sonst Fehler (CONNACK-Code positiv) */
static int broker_connect(atomic_val_t gen)
{
	int64_t deadline;
	int r;

	client_setup();
	connack = false;
	r = mqtt_connect(&client);
	if (r != 0) {
		return r;
	}
	deadline = k_uptime_get() + MQ_CONNACK_MS;
	while (!connack) {
		r = poll_input(MQ_TICK_MS);
		if ((r != 0 && r != -EAGAIN) || reconfigured(gen) || k_uptime_get() > deadline) {
			(void)mqtt_abort(&client);
			return r != 0 && r != -EAGAIN ? r : -ETIMEDOUT;
		}
	}
	if (connack_rc != 0) {
		(void)mqtt_abort(&client);
		return connack_rc; /* MQTT 3.1.1: 1..5 = abgelehnt */
	}
	return 0;
}

static void subscribe(void)
{
	struct mqtt_topic list[ARRAY_SIZE(cmds)];
	static char names_buf[ARRAY_SIZE(cmds)][MQ_TOPIC_MAX];

	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		int n = wf_mq_topic_name(names_buf[i], sizeof(names_buf[i]), prefix, cmds[i].name);

		list[i].topic.utf8 = (const uint8_t *)names_buf[i];
		list[i].topic.size = n > 0 ? (uint32_t)n : 0;
		list[i].qos = MQTT_QOS_0_AT_MOST_ONCE;
	}
	const struct mqtt_subscription_list sub = {
		.list = list, .list_count = ARRAY_SIZE(cmds), .message_id = 1};

	if (mqtt_subscribe(&client, &sub) != 0) {
		LOG_WRN("Befehle nicht abonniert");
	}
}

/* verbunden: senden und empfangen bis Abbruch oder Neukonfiguration */
static void session(atomic_val_t gen)
{
	struct wf_event ev;
	int r;

	set_conn(WF_MQ_CONNECTED);
	LOG_INF("verbunden, Praefix %s", prefix);
	for (int t = 0; t < T_COUNT; t++) {
		wf_mq_reset(&topics[t]);
	}
	/* waehrend der Unterbrechung entstandene Ereignisse nicht nachsenden */
	while (evse_task_event_get(&ev_seq, &ev)) {
	}
	(void)publish("online", "true", 4, true);
	if (cfg.mqtt_commands) {
		subscribe();
	}
	for (;;) {
		r = poll_input(MQ_TICK_MS);
		if (r != 0 && r != -EAGAIN) {
			break;
		}
		if (reconfigured(gen)) {
			/* geordnet: DISCONNECT loest den Last Will nicht aus */
			(void)publish("online", "false", 5, true);
			(void)mqtt_disconnect(&client, NULL);
			r = 0;
			break;
		}
		/* PINGREQ eine Keepalive-Spanne lang unbeantwortet: Broker weg */
		if (client.unacked_ping > 0 && mqtt_keepalive_time_left(&client) == 0) {
			r = -ETIMEDOUT;
			break;
		}
		r = mqtt_live(&client);
		if (r != 0 && r != -EAGAIN) {
			break;
		}
		publish_states();
		publish_events();
	}
	if (r != 0) {
		LOG_WRN("Verbindung getrennt (%d)", r);
		note_error(r);
		(void)mqtt_abort(&client);
	}
	set_conn(WF_MQ_NOT_CONNECTED);
}

static void load_config(void)
{
	netcfg_get(&cfg, NULL);
	if (cfg.mqtt_prefix[0] != '\0') {
		strcpy(prefix, cfg.mqtt_prefix);
	} else {
		char uid[7];

		netcfg_uid(uid);
		snprintk(prefix, sizeof(prefix), "wallfree/%s", uid);
	}
	k_mutex_lock(&st_mtx, K_FOREVER);
	strcpy(st.prefix, prefix);
	k_mutex_unlock(&st_mtx);
}

static void mqtt_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	int32_t backoff = MQ_BACKOFF_MIN_MS;

	for (;;) {
		atomic_val_t gen = atomic_get(&generation);

		load_config();
		if (!cfg.mqtt_enable) {
			set_conn(WF_MQ_OFF);
			(void)k_sem_take(&wake, K_FOREVER);
			backoff = MQ_BACKOFF_MIN_MS;
			continue;
		}
		if (st.conn == WF_MQ_OFF) {
			set_conn(WF_MQ_NOT_CONNECTED);
		}
		int r = broker_connect(gen);

		if (r == 0) {
			backoff = MQ_BACKOFF_MIN_MS;
			session(gen);
			if (reconfigured(gen)) {
				continue;
			}
		} else {
			note_error(r);
			if (r > 0) {
				set_conn(WF_MQ_ERROR);
				LOG_WRN("Broker lehnt ab (CONNACK %d)", r);
			} else {
				LOG_DBG("Verbindung fehlgeschlagen (%d)", r);
			}
			if (reconfigured(gen)) {
				backoff = MQ_BACKOFF_MIN_MS;
				continue;
			}
		}
		/* SOLL-MQTT-03; eine Neukonfiguration weckt sofort */
		(void)k_sem_take(&wake, K_MSEC(backoff));
		backoff = MIN(backoff * 2, MQ_BACKOFF_MAX_MS);
	}
}

K_THREAD_STACK_DEFINE(mqtt_stack, MQ_STACK);
static struct k_thread mqtt_thread;

void mqtt_task_start(void)
{
	k_thread_create(&mqtt_thread, mqtt_stack, K_THREAD_STACK_SIZEOF(mqtt_stack), mqtt_entry,
			NULL, NULL, NULL, MQ_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&mqtt_thread, "mqtt");
}

void mqtt_task_reconfigure(void)
{
	atomic_inc(&generation);
	k_sem_give(&wake);
}

void mqtt_task_status(struct wf_mq_status *out)
{
	k_mutex_lock(&st_mtx, K_FOREVER);
	*out = st;
	k_mutex_unlock(&st_mtx);
}
