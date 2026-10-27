/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Uhrzeit per SNTP (SOLL-TIME-01..04). Eigener Thread mit niedrigster Prioritaet, nicht in
 * der Thread-Ueberwachung: Ein haengender Zeitserver darf keinen Neustart ausloesen. Die
 * Ladelogik benutzt die Uhrzeit nie; ohne Zeitserver bleiben alle Zeiten relativ zum Start.
 *
 * Ablauf: Server bestimmen (ntp_server, sonst Zeitserver aus DHCP Option 42, sonst
 * pool.ntp.org), Name per DNS aufloesen, eine SNTP-Abfrage; Erfolg -> Uhrzeit = Antwort minus
 * Laufzeit (Basis), naechster Abgleich nach 6 h; Fehlschlag -> nach 60 s, verdoppelt bis 1 h.
 * Die Steuerung hat keine Pufferbatterie: nach jedem Start ist die Uhrzeit wieder unbekannt.
 */
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/sntp.h>
#include <wallfree/api/ntp.h>
#include <wallfree/api/sysconf.h>
#include "http_api.h"
#include "netcfg.h"
#include "timesync.h"

LOG_MODULE_REGISTER(timesync, LOG_LEVEL_INF);

#define TS_STACK      2048
#define TS_PRIO       K_LOWEST_APPLICATION_THREAD_PRIO
#define TS_TIMEOUT_MS 3000

K_THREAD_STACK_DEFINE(ts_stack, TS_STACK);
static struct k_thread ts_thread;
static K_SEM_DEFINE(ts_wake, 0, 1);

static struct k_spinlock lock;
static int64_t base_ms;   /* Uhrzeit bei Laufzeit 0, 0 = unbekannt */
static int64_t synced_at; /* Laufzeit des letzten Erfolgs */
static char last_server[WF_NTP_SERVER_MAX + 1];


static void set_base(int64_t b, const char *server)
{
	k_spinlock_key_t k = k_spin_lock(&lock);

	base_ms = b;
	synced_at = b != 0 ? k_uptime_get() : 0;
	snprintf(last_server, sizeof(last_server), "%s", server);
	k_spin_unlock(&lock, k);
	http_api_set_wall_base(b); /* Protokoll: neue Eintraege mit Uhrzeit (SOLL-ERR-12) */
}

int64_t timesync_now_ms(void)
{
	k_spinlock_key_t k = k_spin_lock(&lock);
	int64_t b = base_ms;

	k_spin_unlock(&lock, k);
	return b != 0 ? b + k_uptime_get() : 0;
}

void timesync_json(struct wf_jbuf *b)
{
	k_spinlock_key_t k = k_spin_lock(&lock);
	int64_t bm = base_ms, at = synced_at;
	char srv[sizeof(last_server)];

	memcpy(srv, last_server, sizeof(srv));
	k_spin_unlock(&lock, k);
	if (bm == 0) {
		wf_jb_raw(b, "null");
		return;
	}
	int64_t now = k_uptime_get();

	wf_jb_raw(b, "{");
	wf_jb_key(b, "time_ms", true);
	wf_jb_int(b, bm + now);
	wf_jb_key(b, "synced_ms", false); /* wie lange der letzte Abgleich her ist */
	wf_jb_int(b, now - at);
	wf_jb_key(b, "server", false);
	wf_jb_str(b, srv);
	wf_jb_raw(b, "}");
}

void timesync_notify(void)
{
	k_sem_give(&ts_wake);
}

#if defined(CONFIG_DNS_RESOLVER)
static K_SEM_DEFINE(dns_done, 0, 1);
static struct net_sockaddr_in dns_addr;
static bool dns_ok;

static void dns_cb(enum dns_resolve_status status, struct dns_addrinfo *info, void *user_data)
{
	ARG_UNUSED(user_data);
	if (status == DNS_EAI_INPROGRESS && info != NULL && info->ai_family == NET_AF_INET) {
		memcpy(&dns_addr, &info->ai_addr, sizeof(dns_addr));
		dns_ok = true;
		return;
	}
	if (status != DNS_EAI_INPROGRESS) {
		k_sem_give(&dns_done); /* fertig (DNS_EAI_ALLDONE) oder Fehler */
	}
}
#endif

/* @return 0 und Adresse in @p sa oder < 0 */
static int resolve(const char *host, uint16_t port, struct net_sockaddr_in *sa)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = NET_AF_INET;
	sa->sin_port = net_htons(port);
	if (net_addr_pton(NET_AF_INET, host, &sa->sin_addr) == 0) {
		return 0;
	}
#if defined(CONFIG_DNS_RESOLVER)
	dns_ok = false;
	k_sem_reset(&dns_done);
	if (dns_get_addr_info(host, DNS_QUERY_TYPE_A, NULL, dns_cb, NULL, TS_TIMEOUT_MS) < 0) {
		return -EIO;
	}
	(void)k_sem_take(&dns_done, K_MSEC(TS_TIMEOUT_MS + 1000));
	if (!dns_ok) {
		return -ENOENT;
	}
	sa->sin_addr = dns_addr.sin_addr;
	return 0;
#else
	return -ENOTSUP; /* Simulation: kein DNS (Fallstricke) */
#endif
}

/* Server nach SOLL-TIME-02: eingestellt, sonst DHCP Option 42, sonst pool.ntp.org */
static void choose(const struct wf_sysconf *c, char *host, uint16_t *port)
{
	*port = WF_NTP_PORT;
	if (c->ntp_server[0] != '\0' &&
	    wf_ntp_parse_server(c->ntp_server, strlen(c->ntp_server), host, port) == 0) {
		return;
	}
#if defined(CONFIG_NET_DHCPV4_OPTION_NTP_SERVER)
	struct net_if *iface = net_if_get_default();

	if (iface != NULL && iface->config.dhcpv4.ntp_addr.s_addr != 0) {
		net_addr_ntop(NET_AF_INET, &iface->config.dhcpv4.ntp_addr, host, 16);
		return;
	}
#endif
	strcpy(host, WF_NTP_DEFAULT_SERVER);
}

static int sync_once(const struct wf_sysconf *c)
{
	char host[WF_NTP_SERVER_MAX + 1];
	uint16_t port;
	struct net_sockaddr_in sa;
	struct sntp_time ts;

	choose(c, host, &port);
	int r = resolve(host, port, &sa);

	if (r < 0) {
		LOG_WRN("Zeitserver %s: Name nicht aufgeloest (%d)", host, r);
		return r;
	}
	r = sntp_simple_addr((struct net_sockaddr *)&sa, sizeof(sa), TS_TIMEOUT_MS, &ts);
	if (r < 0) {
		LOG_WRN("Zeitserver %s: keine Antwort (%d)", host, r);
		return r;
	}
	int64_t ms = wf_ntp_epoch_ms(ts.seconds, ts.fraction);

	if (ms == 0) {
		LOG_WRN("Zeitserver %s: unplausible Zeit %llu", host, (unsigned long long)ts.seconds);
		return -ERANGE;
	}
	bool first = timesync_now_ms() == 0;

	set_base(ms - k_uptime_get(), host);
	if (first) {
		LOG_INF("Uhrzeit von %s", host);
	}
	return 0;
}

static void ts_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	unsigned int fails = 1;

	/* erster Versuch nach kurzer Wartezeit fuer DHCP; eine Einstellungsaenderung weckt sofort */
	(void)k_sem_take(&ts_wake, K_SECONDS(10));
	for (;;) {
		struct wf_sysconf c;

		netcfg_get(&c, NULL);
		uint32_t wait_s;

		if (!c.ntp_enable) {
			if (timesync_now_ms() != 0) {
				set_base(0, "");
				LOG_INF("Uhrzeit abgeschaltet (ntp_enable = 0)");
			}
			wait_s = UINT32_MAX;
		} else {
			fails = sync_once(&c) == 0 ? 0 : fails + 1;
			wait_s = wf_ntp_next_s(fails);
		}
		(void)k_sem_take(&ts_wake, wait_s == UINT32_MAX ? K_FOREVER : K_SECONDS(wait_s));
	}
}

void timesync_start(void)
{
	k_thread_create(&ts_thread, ts_stack, K_THREAD_STACK_SIZEOF(ts_stack), ts_entry, NULL,
			NULL, NULL, TS_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&ts_thread, "timesync");
}
