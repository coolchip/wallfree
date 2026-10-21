/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter (10.1, F20, F25, SOLL-SH-19), siehe netcfg.h.
 * Die Zaehlerparameter (meter_*) liegen mit im Satz; sie wertet meter_task
 * beim Start aus. Die MQTT-Parameter (mqtt_*) wirken sofort (SOLL-MQTT-31):
 * Sie werden beim Speichern auch in die laufende Konfiguration uebernommen,
 * und mqtt_task baut die Verbindung neu auf.
 *
 * Telnet: Zephyr startet die Telnet-Shell vor main() und kann den Port zur
 * Laufzeit nicht schliessen. Mit telnet_enable = 0 wird die Kommando-
 * verarbeitung gestoppt (shell_stop); Port 23 nimmt dann noch Verbindungen
 * an, fuehrt aber nichts aus. Ganz entfernen laesst sich Telnet nur im Build
 * (CONFIG_SHELL_BACKEND_TELNET=n).
 *
 * Nur gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h> /* vor dhcpv4.h */
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/hostname.h>
#include <zephyr/shell/shell_telnet.h>
#include "hw.h"
#include "mqtt_task.h"
#include "netcfg.h"
#include "storage.h"

LOG_MODULE_REGISTER(netcfg, LOG_LEVEL_INF);

static struct wf_sysconf active; /* beim Start angewandt */
static struct wf_sysconf saved;  /* gespeichert, wirkt nach Neustart */
static K_MUTEX_DEFINE(mtx);

static void load(struct wf_sysconf *c)
{
	uint8_t buf[WF_SYS_VAL_MAX];
	const char *fix;

	wf_sysconf_defaults(c);
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		int n = storage_read_raw(p->id, buf, sizeof(buf));

		if (n > 0 && wf_sys_decode(c, p->id, buf, (size_t)n) != 0) {
			LOG_WRN("%s: gespeicherter Wert ungueltig, Default", p->name);
		}
	}
	fix = wf_sys_repair(c); /* z. B. feste Adresse widerspruechlich: lieber DHCP als kein Netz */
	if (fix != NULL) {
		LOG_WRN("gespeicherte Konfiguration: %s", fix);
	}
}

static struct net_in_addr in4(uint32_t ip)
{
	struct net_in_addr a = {.s_addr = net_htonl(ip)};

	return a;
}

void netcfg_uid(char out[7])
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *ll = iface ? net_if_get_link_addr(iface) : NULL;

	if (ll != NULL && ll->len >= 3) {
		snprintk(out, 7, "%02x%02x%02x", ll->addr[ll->len - 3], ll->addr[ll->len - 2],
			 ll->addr[ll->len - 1]);
	} else {
		strcpy(out, "000000");
	}
}

static void apply(const struct wf_sysconf *c)
{
	char host[WF_HOSTNAME_MAX + 1];

	if (c->hostname[0] != '\0') {
		strcpy(host, c->hostname);
	} else {
		char uid[7];

		netcfg_uid(uid);
		snprintk(host, sizeof(host), "wallfree-%s", uid);
	}
	(void)net_hostname_set(host, strlen(host));

#if defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	/* PC-Simulation: Host-Sockets, Adresse und DHCP macht der Host */
	ARG_UNUSED(in4);
	LOG_INF("Simulation: Host-Sockets, gespeicherte Adresse (%s) nicht angewandt",
		c->dhcp ? "DHCP" : "fest");
#else
	struct net_if *iface = net_if_get_default();

	if (iface != NULL) {
		if (c->dhcp) {
			net_dhcpv4_start(iface);
		} else {
			struct net_in_addr ip = in4(c->ip), mask = in4(c->netmask);

			if (net_if_ipv4_addr_add(iface, &ip, NET_ADDR_MANUAL, 0) == NULL ||
			    !net_if_ipv4_set_netmask_by_addr(iface, &ip, &mask)) {
				LOG_ERR("feste Adresse nicht gesetzt");
			}
			if (c->gateway != 0) {
				struct net_in_addr gw = in4(c->gateway);

				net_if_ipv4_set_gw(iface, &gw);
			}
		}
	}
#endif
	if (!c->telnet) {
		(void)shell_stop(shell_backend_telnet_get_ptr());
		LOG_INF("Telnet-Shell gestoppt (telnet_enable = 0)");
	}
	LOG_INF("Netz: %s, Hostname %s", c->dhcp ? "DHCP" : "fest", host);
}

/* gleich, wenn alle Parameter gleich kodiert sind (ohne Fuellbytes) */
static bool same(const struct wf_sysconf *x, const struct wf_sysconf *y)
{
	uint8_t a[WF_SYS_VAL_MAX], b[WF_SYS_VAL_MAX];

	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		size_t na = wf_sys_encode(x, p, a);
		size_t nb = wf_sys_encode(y, p, b);

		if (na != nb || memcmp(a, b, na) != 0) {
			return false;
		}
	}
	return true;
}

/* ------------------------------------------------ Netzwerk-LED (SOLL-LED-06) */

#define NETLED_MS 500 /* 1 Hz: 500 ms an, 500 ms aus */

static void netled_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(netled_work, netled_fn);
static bool netled_on;

/* verbunden = Link und IPv4-Adresse (fest oder per DHCP) */
static bool net_connected(void)
{
#if defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	return hw_sim_link();
#else
	struct net_if *iface = net_if_get_default();

	return iface != NULL && net_if_is_carrier_ok(iface) &&
	       net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED) != NULL;
#endif
}

static void netled_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	netled_on = net_connected() ? true : !netled_on;
	hw_gpio_net_led(netled_on);
	k_work_schedule(&netled_work, K_MSEC(NETLED_MS));
}

void netcfg_init(void)
{
	k_work_schedule(&netled_work, K_NO_WAIT);
	k_mutex_lock(&mtx, K_FOREVER);
	load(&saved);
	active = saved;
	k_mutex_unlock(&mtx);
	apply(&active);
}

void netcfg_get(struct wf_sysconf *a, struct wf_sysconf *s)
{
	k_mutex_lock(&mtx, K_FOREVER);
	if (a != NULL) {
		*a = active;
	}
	if (s != NULL) {
		*s = saved;
	}
	k_mutex_unlock(&mtx);
}

bool netcfg_restart_required(void)
{
	k_mutex_lock(&mtx, K_FOREVER);
	bool r = !same(&active, &saved);

	k_mutex_unlock(&mtx);
	return r;
}

/* geaenderte Werte speichern; mtx gehalten */
static const char *commit(const struct wf_sysconf *next)
{
	uint8_t a[WF_SYS_VAL_MAX], b[WF_SYS_VAL_MAX];
	bool live = false;

	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		size_t na = wf_sys_encode(next, p, a);
		size_t nb = wf_sys_encode(&saved, p, b);

		if (na == nb && memcmp(a, b, na) == 0) {
			continue;
		}
		if (storage_save_raw(p->id, a, na) < 0) {
			return "Speichern fehlgeschlagen";
		}
		live |= p->id >= WF_SYS_ID_LIVE;
	}
	saved = *next;
	if (live) {
		wf_sys_take_live(&active, &saved);
		mqtt_task_reconfigure();
	}
	return NULL;
}

struct set_ctx {
	struct wf_sysconf next;
	struct wf_jbuf *out;
	int errors;
};

static int set_visit(void *ctx, const char *key, const struct wf_jval *v)
{
	struct set_ctx *c = ctx;
	const struct wf_syspar *p = wf_sys_find(key);
	const char *err = p == NULL ? "unbekannter Parameter" : wf_sys_set(&c->next, p, v);

	if (err != NULL) {
		wf_jb_raw(c->out, c->errors == 0 ? "{\"errors\":{" : "");
		wf_jb_key(c->out, key, c->errors == 0);
		wf_jb_str(c->out, err);
		c->errors++;
	}
	return 0;
}

int netcfg_set_json(const char *body, size_t len, struct wf_jbuf *out)
{
	struct set_ctx c = {.out = out};
	char sb[256];

	k_mutex_lock(&mtx, K_FOREVER);
	c.next = saved;
	if (wf_json_object(body, len, sb, sizeof(sb), set_visit, &c) != 0) {
		k_mutex_unlock(&mtx);
		wf_jb_init(out, out->p, out->size);
		wf_jb_raw(out, "{\"error\":\"ungueltiges JSON (erwartet: flaches Objekt)\"}");
		return 400;
	}
	if (c.errors > 0) {
		k_mutex_unlock(&mtx);
		wf_jb_raw(out, "}}");
		return 400;
	}
	const char *err = wf_sys_check(&c.next);

	if (err == NULL) {
		err = commit(&c.next);
	}
	bool restart = !same(&active, &saved);

	k_mutex_unlock(&mtx);
	if (err != NULL) {
		wf_jb_raw(out, "{\"error\":");
		wf_jb_str(out, err);
		wf_jb_raw(out, "}");
		return 400;
	}
	wf_jb_raw(out, restart ? "{\"restart_required\":true}" : "{\"restart_required\":false}");
	return 200;
}

const char *netcfg_set_text(const char *name, const char *text)
{
	const struct wf_syspar *p = wf_sys_find(name);
	struct wf_jval v = {.type = WF_J_STR, .str = text, .str_len = strlen(text)};

	if (p == NULL) {
		return "unbekannter Parameter";
	}
	if (p->type == WF_SYS_CODE) {
		char *end;

		v.type = WF_J_NUM;
		v.num = strtol(text, &end, 10);
		if (*text == '\0' || *end != '\0') {
			return "Zahl erwartet";
		}
	} else if (p->type == WF_SYS_BOOL) {
		v.type = WF_J_NUM;
		if (strcmp(text, "1") == 0 || strcmp(text, "true") == 0) {
			v.num = 1;
		} else if (strcmp(text, "0") == 0 || strcmp(text, "false") == 0) {
			v.num = 0;
		} else {
			return "0/1 erwartet";
		}
	}
	k_mutex_lock(&mtx, K_FOREVER);
	struct wf_sysconf next = saved;
	const char *err = wf_sys_set(&next, p, &v);

	if (err == NULL) {
		err = wf_sys_check(&next);
	}
	if (err == NULL) {
		err = commit(&next);
	}
	k_mutex_unlock(&mtx);
	return err;
}

int netcfg_import_orig(wf_orig_lookup_t lookup, void *ctx, const char **err)
{
	k_mutex_lock(&mtx, K_FOREVER);
	struct wf_sysconf next = saved;
	int n = wf_sys_import_orig(&next, lookup, ctx, err);

	if (n > 0) {
		*err = commit(&next);
		if (*err != NULL) {
			n = -1;
		}
	}
	k_mutex_unlock(&mtx);
	return n;
}
