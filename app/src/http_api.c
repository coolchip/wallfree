/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP- und WebSocket-API (Soll-Spezifikation 12.1, SOLL-API-01..07/30),
 * erster Umfang fuer evcc ueber die Vorlage tinkerforge-warp-ws.
 *
 *  - Alle Ressourcen mit HTTP-Digest (MD5, qop=auth). Im Werkszugang
 *    beantwortet die API nur /credentials, sonst 403.
 *  - WebSocket /ws: Zephyr bestaetigt das Upgrade (101), bevor die Anwendung
 *    den Aufbau sieht. Die Anmeldung wird danach anhand der mitgesendeten
 *    Authorization-Kopfzeile geprueft (evcc sendet sie nach der ersten
 *    Challenge vorab); ohne gueltige Anmeldung wird die Verbindung ohne
 *    Daten geschlossen.
 *  - Schreibzugriffe laufen ueber die Parametertabelle mit Grenzpruefung (E14).
 *
 * Alle Ressourcen-Callbacks laufen im Thread des Zephyr-HTTP-Servers. Nur
 * gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/websocket.h>
#include <zephyr/sys/reboot.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/api/ratelimit.h>
#include <wallfree/evse/persist.h>
#include "auth.h"
#include "evse_task.h"
#include "hw.h"
#include "netcfg.h"
#include "http_api.h"
#include "storage.h"

LOG_MODULE_REGISTER(http_api, LOG_LEVEL_INF);

#define HTTP_PORT      80
#define BODY_MAX       1024
#define OUT_MAX        1536
#define WS_MAX         3     /* SOLL-API-07: mindestens 2 */
#define WS_PERIOD_MS   100
#define WS_STACK       2048
#define WS_PRIO        11
#define WS_SEND_MS     200

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_auth, "Authorization");

static char out[OUT_MAX];          /* Antwortpuffer (nur Server-Thread) */
static char challenge[160];
static struct http_header hdr_auth = {.name = "WWW-Authenticate", .value = challenge};
static char retry_after[12];
static struct http_header hdr_retry = {.name = "Retry-After", .value = retry_after};
static struct wf_rl rl; /* Bremse je Absender (nur Server-Thread) */

/* IPv4-Absender eines Sockets, 0 wenn unbekannt */
static uint32_t peer_addr(int fd)
{
	struct net_sockaddr_storage ss;
	net_socklen_t len = sizeof(ss);

	if (zsock_getpeername(fd, (struct net_sockaddr *)&ss, &len) != 0 ||
	    ss.ss_family != NET_AF_INET) {
		return 0;
	}
	return net_ntohl(net_sin((struct net_sockaddr *)&ss)->sin_addr.s_addr);
}

/*
 * Anmeldung pruefen mit Bremse: gesperrte Absender werden auch mit
 * richtigem Passwort abgewiesen (sonst bremst die Sperre nichts).
 * @return Ergebnis; *wait_ms > 0, wenn gesperrt
 */
static enum wf_auth checked_auth(uint32_t addr, const char *hdr, const char *method,
				 const char *uri, int64_t *wait_ms)
{
	int64_t now = k_uptime_get();

	*wait_ms = wf_rl_blocked(&rl, addr, now);
	if (*wait_ms > 0) {
		return WF_AUTH_BAD;
	}
	enum wf_auth a = auth_check(hdr, method, uri);

	if (a == WF_AUTH_BAD) {
		wf_rl_fail(&rl, addr, now);
		LOG_WRN("Anmeldung fehlgeschlagen von %u.%u.%u.%u", addr >> 24, (addr >> 16) & 255,
			(addr >> 8) & 255, addr & 255);
	} else if (a == WF_AUTH_OK) {
		wf_rl_ok(&rl, addr);
	}
	return a;
}

/* ------------------------------------------------ gemeinsamer Ablauf */

struct res;
typedef void (*res_fn_t)(struct http_client_ctx *client, struct res *r,
			 struct http_response_ctx *rsp);

struct res {
	res_fn_t fn;
	bool allow_factory;  /* auch im Werkszugang (nur /credentials) */
	/* Zustand der laufenden Anfrage */
	bool decided;
	bool responded;
	bool too_long;
	size_t body_len;
	size_t cursor;       /* fuer Antworten in Teilen */
	char *body;          /* nur POST-Ressourcen, mit Platz fuer NUL */
	size_t body_size;
};

static const char *find_header(const struct http_request_ctx *req, const char *name)
{
	for (size_t i = 0; i < req->header_count; i++) {
		if (strcasecmp(req->headers[i].name, name) == 0) {
			return req->headers[i].value;
		}
	}
	return NULL;
}

static void respond(struct http_response_ctx *rsp, enum http_status status, size_t len)
{
	rsp->status = status;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = len;
	rsp->final_chunk = true;
}

static void respond_json(struct http_response_ctx *rsp, enum http_status status,
			 const struct wf_jbuf *b)
{
	if (b->overflow) {
		int n = snprintf(out, sizeof(out), "{\"error\":\"Antwort zu gross\"}");

		respond(rsp, HTTP_500_INTERNAL_SERVER_ERROR, (size_t)n);
		return;
	}
	respond(rsp, status, b->len);
}

static void respond_error(struct http_response_ctx *rsp, enum http_status status,
			  const char *text)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_error(&b, text);
	respond_json(rsp, status, &b);
}

static bool gate(struct http_client_ctx *client, struct res *r,
		 const struct http_request_ctx *req, struct http_response_ctx *rsp)
{
	const char *method = http_method_str(client->method);
	int64_t wait_ms;
	enum wf_auth a = checked_auth(peer_addr(client->fd), find_header(req, "Authorization"),
				      method, (const char *)client->url_buffer, &wait_ms);

	if (wait_ms > 0) {
		snprintf(retry_after, sizeof(retry_after), "%u", (unsigned int)(wait_ms / 1000 + 1));
		rsp->headers = &hdr_retry;
		rsp->header_count = 1;
		respond_error(rsp, HTTP_429_TOO_MANY_REQUESTS,
			      "zu viele Fehlversuche, spaeter erneut versuchen");
		return false;
	}

	if (req->headers_status == HTTP_HEADER_STATUS_DROPPED && a != WF_AUTH_OK) {
		LOG_WRN("Authorization-Kopfzeile zu lang");
	}
	if (a != WF_AUTH_OK) {
		if (auth_challenge(a == WF_AUTH_STALE, challenge, sizeof(challenge)) < 0) {
			respond_error(rsp, HTTP_500_INTERNAL_SERVER_ERROR, "Challenge");
			return false;
		}
		rsp->headers = &hdr_auth;
		rsp->header_count = 1;
		respond_error(rsp, HTTP_401_UNAUTHORIZED, "Anmeldung erforderlich");
		return false;
	}
	if (auth_must_change() && !r->allow_factory) {
		respond_error(rsp, HTTP_403_FORBIDDEN,
			      "Werkszugang: zuerst Zugangsdaten aendern (POST /credentials)");
		return false;
	}
	return true;
}

static int dyn_cb(struct http_client_ctx *client, enum http_transaction_status status,
		  const struct http_request_ctx *req, struct http_response_ctx *rsp,
		  void *user_data)
{
	struct res *r = user_data;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		r->decided = false;
		r->responded = false;
		r->too_long = false;
		r->body_len = 0;
		r->cursor = 0;
		return 0;
	}
	if (!r->decided) {
		/* Kopfzeilen gibt es nur im ersten Aufruf */
		r->decided = true;
		if (!gate(client, r, req, rsp)) {
			r->responded = true;
			return 0;
		}
	}
	if (r->responded) {
		rsp->final_chunk = true; /* restlichen Koerper verwerfen */
		return 0;
	}
	if (client->method == HTTP_POST && req->data_len > 0) {
		if (r->body_len + req->data_len >= r->body_size) {
			r->too_long = true;
		} else {
			memcpy(r->body + r->body_len, req->data, req->data_len);
			r->body_len += req->data_len;
		}
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	r->body[r->body_len] = '\0';
	if (r->too_long) {
		r->responded = true;
		respond_error(rsp, HTTP_413_PAYLOAD_TOO_LARGE, "Anfrage zu gross");
		return 0;
	}
	r->fn(client, r, rsp);
	return 0;
}

#define JSON_RESOURCE(_id, _path, _methods, _fn, _factory, _body)                                  \
	static char _id##_body[(_body) + 1];                                                       \
	static struct res _id##_res = {.fn = _fn, .allow_factory = _factory,                       \
				       .body = _id##_body, .body_size = sizeof(_id##_body)};      \
	static struct http_resource_detail_dynamic _id##_detail = {                                \
		.common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                     \
			   .bitmask_of_supported_http_methods = (_methods),                        \
			   .content_type = "application/json"},                                    \
		.cb = dyn_cb,                                                                      \
		.user_data = &_id##_res,                                                           \
	};                                                                                         \
	HTTP_RESOURCE_DEFINE(_id, wallfree_http, _path, &_id##_detail)

static uint16_t http_port = HTTP_PORT;
HTTP_SERVICE_DEFINE(wallfree_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL, NULL,
		    NULL);

/* ------------------------------------------------ /info (SOLL-API-01/02) */

static void info_name(struct http_client_ctx *client, struct res *r,
		      struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;
	char uid[8] = "000000";
	struct net_if *iface = net_if_get_default();

	if (iface != NULL) {
		struct net_linkaddr *ll = net_if_get_link_addr(iface);

		if (ll != NULL && ll->len >= 3) {
			snprintf(uid, sizeof(uid), "%02x%02x%02x", ll->addr[ll->len - 3],
				 ll->addr[ll->len - 2], ll->addr[ll->len - 1]);
		}
	}
	wf_jb_init(&b, out, sizeof(out));
	wf_jb_raw(&b, "{");
	wf_jb_key(&b, "name", true);
	wf_jb_str(&b, net_hostname_get());
	/* F22: Typ warp2, damit evcc keine Phasenumschaltung anspricht */
	wf_jb_key(&b, "type", false);
	wf_jb_str(&b, "warp2");
	wf_jb_key(&b, "display_type", false);
	wf_jb_str(&b, "Wallfree");
	wf_jb_key(&b, "uid", false);
	wf_jb_str(&b, uid);
	wf_jb_raw(&b, "}");
	respond_json(rsp, HTTP_200_OK, &b);
}

static void info_features(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	/* kein Zaehler, kein NFC, keine Phasenumschaltung */
	strcpy(out, "[]");
	respond(rsp, HTTP_200_OK, 2);
}

JSON_RESOURCE(info_name_r, "/info/name", BIT(HTTP_GET), info_name, false, 0);
JSON_RESOURCE(info_features_r, "/info/features", BIT(HTTP_GET), info_features, false, 0);

/* ------------------------------------------------ /evse (SOLL-API-03..05/30) */

static K_SEM_DEFINE(ws_wake, 0, 1);

static void evse_state(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_state(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void external_current(struct http_client_ctx *client, struct res *r,
			     struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	if (client->method == HTTP_POST) {
		int32_t ma;

		if (api_parse_current(r->body, r->body_len, &ma) != 0) {
			respond_error(rsp, HTTP_400_BAD_REQUEST,
				      "erwartet {\"current\": mA} oder mA als Zahl");
			return;
		}
		/* Slot external, nur RAM (SOLL-NVM-03); 0 = Laden aus */
		int err = evse_param_set_by_name(evse_acquire(), "external_current", ma,
						 k_uptime_get());

		evse_release();
		if (err != EVSE_PARAM_OK) {
			respond_error(rsp, HTTP_400_BAD_REQUEST, evse_param_strerror(err));
			return;
		}
		k_sem_give(&ws_wake);
	}
	wf_jb_init(&b, out, sizeof(out));
	api_json_external_current(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void persist_written(void *ctx, const struct evse_param *p, int32_t v)
{
	int *pending = ctx;

	/* Lock-Reihenfolge Ladelogik -> Speicher wie in storage.c */
	if (storage_save_param(p, v) == NVM_PENDING) {
		(*pending)++;
	}
}

static void evse_params(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	if (client->method == HTTP_POST) {
		int pending = 0;
		struct evse *e = evse_acquire();
		int st = api_set_params(e, r->body, r->body_len, k_uptime_get(), persist_written,
					&pending, &b);

		evse_release();
		k_sem_give(&ws_wake);
		respond_json(rsp, st == 200 ? HTTP_200_OK : HTTP_400_BAD_REQUEST, &b);
		return;
	}
	api_json_params(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void param_info(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	/* in Teilen: der Server ruft erneut auf, bis final_chunk gesetzt ist */
	wf_jb_init(&b, out, sizeof(out));
	r->cursor = api_json_param_info(r->cursor, &b);
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor > evse_param_count();
}

JSON_RESOURCE(evse_state_r, "/evse/state", BIT(HTTP_GET), evse_state, false, 0);
JSON_RESOURCE(external_current_r, "/evse/external_current", BIT(HTTP_GET) | BIT(HTTP_POST),
	      external_current, false, 64);
JSON_RESOURCE(evse_params_r, "/evse/params", BIT(HTTP_GET) | BIT(HTTP_POST), evse_params,
	      false, BODY_MAX);
JSON_RESOURCE(param_info_r, "/evse/param_info", BIT(HTTP_GET), param_info, false, 0);

/* ------------------------------------------------ /credentials (SOLL-API-07) */

struct cred_ctx {
	char user[WF_DIGEST_USER_MAX + 1];
	char password[AUTH_PW_MAX + 1];
	bool bad;
};

static int cred_visit(void *ctx, const char *key, const struct wf_jval *v)
{
	struct cred_ctx *c = ctx;
	char *dst;
	size_t size;

	if (strcmp(key, "user") == 0) {
		dst = c->user;
		size = sizeof(c->user);
	} else if (strcmp(key, "password") == 0) {
		dst = c->password;
		size = sizeof(c->password);
	} else {
		c->bad = true;
		return 0;
	}
	if (v->type != WF_J_STR || v->str_len >= size) {
		c->bad = true;
		return 0;
	}
	memcpy(dst, v->str, v->str_len + 1);
	return 0;
}

static void credentials(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	if (client->method == HTTP_POST) {
		struct cred_ctx c = {0};
		char sb[160];

		if (wf_json_object(r->body, r->body_len, sb, sizeof(sb), cred_visit, &c) != 0 ||
		    c.bad || c.user[0] == '\0' || c.password[0] == '\0') {
			respond_error(rsp, HTTP_400_BAD_REQUEST,
				      "erwartet {\"user\": Text, \"password\": Text}");
			return;
		}
		const char *err = auth_change(c.user, c.password);

		memset(&c, 0, sizeof(c));
		memset(r->body, 0, r->body_size);
		if (err != NULL) {
			respond_error(rsp, HTTP_400_BAD_REQUEST, err);
			return;
		}
	}
	wf_jb_init(&b, out, sizeof(out));
	wf_jb_raw(&b, "{");
	wf_jb_key(&b, "must_change", true);
	wf_jb_raw(&b, auth_must_change() ? "true" : "false");
	wf_jb_raw(&b, "}");
	respond_json(rsp, HTTP_200_OK, &b);
}

JSON_RESOURCE(credentials_r, "/credentials", BIT(HTTP_GET) | BIT(HTTP_POST), credentials, true, 256);

/* ------------------------------------------------ /system (10.1 net_*, telnet_enable) */

static void system_config(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	if (client->method == HTTP_POST) {
		/* alle Werte gueltig oder keiner; wirkt nach Neustart */
		int st = netcfg_set_json(r->body, r->body_len, &b);

		respond_json(rsp, st == 200 ? HTTP_200_OK : HTTP_400_BAD_REQUEST, &b);
		return;
	}
	struct wf_sysconf a, sv;

	netcfg_get(&a, &sv);
	wf_jb_raw(&b, "{\"active\":");
	wf_sys_json(&a, &b);
	wf_jb_raw(&b, ",\"saved\":");
	wf_sys_json(&sv, &b);
	wf_jb_raw(&b, netcfg_restart_required() ? ",\"restart_required\":true}"
						 : ",\"restart_required\":false}");
	respond_json(rsp, HTTP_200_OK, &b);
}

static void system_config_info(struct http_client_ctx *client, struct res *r,
			       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	wf_sys_info_json(&b);
	respond_json(rsp, HTTP_200_OK, &b);
}

static void reboot_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	/* SOLL-RT-20: sicherer Zustand vor dem Neustart */
	hw_safe_state_regs();
	sys_reboot(SYS_REBOOT_COLD);
}
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_fn);

static void system_restart(struct http_client_ctx *client, struct res *r,
			   struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	bool ok = evse_flash_erase_allowed(evse_acquire());

	evse_release();
	if (!ok) {
		respond_error(rsp, HTTP_409_CONFLICT, "Neustart nur ohne Fahrzeug in S1");
		return;
	}
	/* Antwort noch senden lassen, dann neu starten */
	k_work_schedule(&reboot_work, K_MSEC(500));
	strcpy(out, "{}");
	respond(rsp, HTTP_200_OK, 2);
}

JSON_RESOURCE(system_config_r, "/system/config", BIT(HTTP_GET) | BIT(HTTP_POST), system_config,
	      false, 512);
JSON_RESOURCE(system_config_info_r, "/system/config_info", BIT(HTTP_GET), system_config_info,
	      false, 0);
JSON_RESOURCE(system_restart_r, "/system/restart", BIT(HTTP_POST), system_restart, false, 16);

/* ------------------------------------------------ WebSocket /ws (SOLL-API-06) */

static struct {
	int sock;           /* -1 = frei */
	bool full_sync;     /* alle Topics senden */
} ws[WS_MAX];
static K_MUTEX_DEFINE(ws_mtx);
static uint8_t ws_hs_buf[256];

/*
 * Absender beim WebSocket-Aufbau: Zephyr uebergibt nur den WebSocket-Socket
 * (ohne getpeername). request_ctx->headers zeigt auf das Kopfzeilenfeld des
 * Client-Kontexts (http_server_ws.c, populate_request_ctx, Zephyr 4.4.2),
 * daraus der TCP-Socket. Abhaengigkeit von Zephyr-Interna, bei Updates pruefen.
 */
static uint32_t ws_peer(const struct http_request_ctx *req)
{
	if (req->header_count == 0) {
		return 0;
	}
	const struct http_client_ctx *c = (const struct http_client_ctx *)(
		(const char *)req->headers -
		offsetof(struct http_client_ctx, header_capture_ctx.headers));

	return peer_addr(c->fd);
}

static int ws_setup(int sock, struct http_request_ctx *req, void *user_data)
{
	ARG_UNUSED(user_data);
	int64_t wait_ms;
	enum wf_auth a = checked_auth(ws_peer(req), find_header(req, "Authorization"), "GET", "/ws",
				      &wait_ms);

	if (a != WF_AUTH_OK || auth_must_change()) {
		LOG_WRN("WebSocket ohne gueltige Anmeldung geschlossen");
		return -EACCES; /* der Server schliesst die Verbindung */
	}
	k_mutex_lock(&ws_mtx, K_FOREVER);
	for (int i = 0; i < WS_MAX; i++) {
		if (ws[i].sock < 0) {
			ws[i].sock = sock;
			ws[i].full_sync = true;
			k_mutex_unlock(&ws_mtx);
			k_sem_give(&ws_wake);
			return 0;
		}
	}
	k_mutex_unlock(&ws_mtx);
	LOG_WRN("WebSocket: alle %d Plaetze belegt", WS_MAX);
	return -ENOMEM;
}

static struct http_resource_detail_websocket ws_detail = {
	.common = {.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
		   .bitmask_of_supported_http_methods = BIT(HTTP_GET)},
	.cb = ws_setup,
	.data_buffer = ws_hs_buf,
	.data_buffer_len = sizeof(ws_hs_buf),
};
HTTP_RESOURCE_DEFINE(ws_r, wallfree_http, "/ws", &ws_detail);

static void ws_close(int i)
{
	(void)websocket_unregister(ws[i].sock);
	ws[i].sock = -1;
}

/* Eingehende Rahmen lesen und verwerfen; Abbau erkennen */
static void ws_drain(int i)
{
	struct zsock_pollfd pfd = {.fd = ws[i].sock, .events = ZSOCK_POLLIN};
	uint8_t buf[64];

	while (zsock_poll(&pfd, 1, 0) > 0) {
		uint32_t type;
		uint64_t remaining;

		if (pfd.revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
			ws_close(i);
			return;
		}
		int n = websocket_recv_msg(ws[i].sock, buf, sizeof(buf), &type, &remaining, 0);

		if (n < 0 && n != -EAGAIN) {
			ws_close(i);
			return;
		}
		if (n == 0 && (type & WEBSOCKET_FLAG_CLOSE)) {
			ws_close(i);
			return;
		}
		if (n <= 0) {
			return;
		}
	}
}

#define WS_TOPICS 2
static char ws_last[WS_TOPICS][160];
static char ws_cur[WS_TOPICS][160];
static char ws_frame[256];
static const char *const ws_topic[WS_TOPICS] = {"evse/state", "evse/external_current"};

static void ws_render(void)
{
	struct wf_jbuf b[WS_TOPICS];
	struct evse *e;

	for (int t = 0; t < WS_TOPICS; t++) {
		wf_jb_init(&b[t], ws_cur[t], sizeof(ws_cur[t]));
	}
	e = evse_acquire();
	api_json_state(e, &b[0]);
	api_json_external_current(e, &b[1]);
	evse_release();
}

static void ws_send(int i, int t)
{
	int n = snprintf(ws_frame, sizeof(ws_frame), "{\"topic\":\"%s\",\"payload\":%s}",
			 ws_topic[t], ws_cur[t]);

	if (n < 0 || (size_t)n >= sizeof(ws_frame) ||
	    websocket_send_msg(ws[i].sock, (const uint8_t *)ws_frame, (size_t)n,
			       WEBSOCKET_OPCODE_DATA_TEXT, false, true, WS_SEND_MS) < 0) {
		ws_close(i);
	}
}

static void ws_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		(void)k_sem_take(&ws_wake, K_MSEC(WS_PERIOD_MS));
		ws_render();

		k_mutex_lock(&ws_mtx, K_FOREVER);
		for (int i = 0; i < WS_MAX; i++) {
			if (ws[i].sock >= 0) {
				ws_drain(i);
			}
		}
		for (int t = 0; t < WS_TOPICS; t++) {
			bool changed = strcmp(ws_cur[t], ws_last[t]) != 0;

			for (int i = 0; i < WS_MAX; i++) {
				if (ws[i].sock >= 0 && (changed || ws[i].full_sync)) {
					ws_send(i, t);
				}
			}
			strcpy(ws_last[t], ws_cur[t]);
		}
		for (int i = 0; i < WS_MAX; i++) {
			ws[i].full_sync = false;
		}
		k_mutex_unlock(&ws_mtx);
	}
}

K_THREAD_STACK_DEFINE(ws_stack, WS_STACK);
static struct k_thread ws_thread;

int http_api_start(void)
{
	for (int i = 0; i < WS_MAX; i++) {
		ws[i].sock = -1;
	}
	wf_rl_init(&rl);
	auth_init();
	k_thread_create(&ws_thread, ws_stack, K_THREAD_STACK_SIZEOF(ws_stack), ws_entry, NULL,
			NULL, NULL, WS_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&ws_thread, "ws_push");
	int r = http_server_start();

	if (r != 0) {
		LOG_ERR("HTTP-Server: %d", r);
	}
	return r;
}
