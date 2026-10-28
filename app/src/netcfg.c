/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter (10.1, F20, F25, SOLL-SH-19), siehe netcfg.h.
 * Die Zaehlerparameter (meter_*) liegen mit im Satz; sie wertet meter_task
 * beim Start aus. Die MQTT-Parameter (mqtt_*) wirken sofort (SOLL-MQTT-31):
 * Sie werden beim Speichern auch in die laufende Konfiguration uebernommen,
 * und mqtt_task baut die Verbindung neu auf.
 *
 * Telnet: Zephyr startet die Telnet-Shell vor main() ohne Moeglichkeit, das
 * abzuschalten. Mit telnet_enable = 0 stoppt netcfg die Shell (shell_stop) und
 * schliesst danach Port 23: Der Telnet-Server ist ein Socket-Dienst, er wird
 * an seinem Listen-Socket erkannt und mit net_socket_service_close() samt
 * Sockets abgemeldet. Ganz entfernen laesst sich Telnet nur im Build
 * (CONFIG_SHELL_BACKEND_TELNET=n).
 *
 * Nur gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h> /* vor dhcpv4.h */
#include <zephyr/net/dhcpv4.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/net/ethernet.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/socket_service.h>
#include <zephyr/shell/shell_telnet.h>
#include "hw.h"
#include "mqtt_task.h"
#include "netcfg.h"
#include "storage.h"
#include "timesync.h"

LOG_MODULE_REGISTER(netcfg, LOG_LEVEL_INF);

static struct wf_sysconf active; /* beim Start angewandt */
static struct wf_sysconf saved;  /* gespeichert, wirkt nach Neustart */
static K_MUTEX_DEFINE(mtx);

/* ------------------------------------------------ Telnet schliessen */

struct telnet_find {
	int fd;
	const struct net_socket_service_desc *svc;
};

static void telnet_match(const struct net_socket_service_desc *svc, void *user_data)
{
	struct telnet_find *f = user_data;

	if (svc->pev_len == SHELL_TELNET_POLLFD_COUNT && svc->pev[0].event.fd == f->fd) {
		f->svc = svc;
	}
}

/* Vor der Initialisierung steht im Telnet-Kontext fd 0: nur der Socket auf dem Telnet-Port zaehlt */
static bool telnet_listening(int fd)
{
	struct net_sockaddr_in a;
	net_socklen_t len = sizeof(a);

	return fd >= 0 && zsock_getsockname(fd, (struct net_sockaddr *)&a, &len) == 0 &&
	       a.sin_family == NET_AF_INET && net_ntohs(a.sin_port) == CONFIG_SHELL_TELNET_PORT;
}

/* Der Shell-Thread oeffnet den Port erst nach dem Start; bis dahin erneut versuchen */
#define TELNET_CLOSE_TRIES 40
#define TELNET_CLOSE_STEP  K_MSEC(250)

static void telnet_close_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(telnet_close_work, telnet_close_fn);

static void telnet_close_fn(struct k_work *w)
{
	static int tries;
	struct shell_telnet *t = shell_backend_telnet_get_ptr()->iface->ctx;
	struct telnet_find f = {.fd = t->fds[0].fd}; /* IPv4-Listen-Socket */

	if (telnet_listening(f.fd)) {
		net_socket_service_foreach(telnet_match, &f);
	}
	if (f.svc != NULL && net_socket_service_close(f.svc) == 0) {
		for (int i = 0; i < SHELL_TELNET_POLLFD_COUNT; i++) {
			t->fds[i].fd = -1;
		}
		LOG_INF("Telnet: Port %d geschlossen (telnet_enable = 0)", CONFIG_SHELL_TELNET_PORT);
		return;
	}
	if (++tries < TELNET_CLOSE_TRIES) {
		k_work_reschedule(k_work_delayable_from_work(w), TELNET_CLOSE_STEP);
	} else {
		LOG_ERR("Telnet: Port %d nicht geschlossen", CONFIG_SHELL_TELNET_PORT);
	}
}

static void load(struct wf_sysconf *c)
{
	uint8_t buf[WF_SYS_VAL_MAX];
	const char *fix;

	wf_sysconf_defaults(c);
	c->telnet = IS_ENABLED(CONFIG_WALLFREE_TELNET_DEFAULT);
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		int n = storage_read_raw(p->id, buf, sizeof(buf));

		if (n > 0 && wf_sys_decode(c, p->id, buf, (size_t)n) != 0) {
			LOG_WRN("%s: gespeicherter Wert ungueltig, Default", p->name);
		}
	}
	/* mqtt_broker frueher als IPv4 unter 3021 (wf_sys_migrate_broker) */
	if (storage_read_raw(wf_sys_find("mqtt_broker")->id, buf, sizeof(buf)) <= 0) {
		int n = storage_read_raw(WF_SYS_ID_BROKER_OLD, buf, sizeof(buf));

		if (n > 0) {
			wf_sys_migrate_broker(c, buf, (size_t)n);
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

#if !defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
/*
 * Geraete-MAC aus dem OTP (Spezifikation 10.1) vor DHCP und Hostname setzen. Der Treiber
 * uebernimmt sie in die MAC-Register; die Schnittstelle zeigt auf seinen Puffer.
 */
static void use_device_mac(struct net_if *iface)
{
	const struct device *dev = net_if_get_device(iface);
	const struct ethernet_api *api = dev->api;
	struct ethernet_config cfg;

	if (!hw_device_mac(cfg.mac_address.addr)) {
		LOG_WRN("keine Geraete-MAC im OTP, erzeugte Adresse bleibt");
		return;
	}
	if (api->set_config == NULL ||
	    api->set_config(dev, ETHERNET_CONFIG_TYPE_MAC_ADDRESS, &cfg) != 0) {
		LOG_ERR("Geraete-MAC nicht gesetzt");
		return;
	}
	if (memcmp(net_if_get_link_addr(iface)->addr, cfg.mac_address.addr, 6) != 0) {
		(void)net_if_set_link_addr(iface, (uint8_t *)cfg.mac_address.addr, 6,
					   NET_LINK_ETHERNET);
	}
}
#endif

void netcfg_mac(char out[18])
{
	struct net_if *iface = net_if_get_default();
	struct net_linkaddr *ll = iface ? net_if_get_link_addr(iface) : NULL;

	out[0] = '\0';
	if (ll != NULL && ll->len == 6) {
		snprintk(out, 18, "%02x:%02x:%02x:%02x:%02x:%02x", ll->addr[0], ll->addr[1],
			 ll->addr[2], ll->addr[3], ll->addr[4], ll->addr[5]);
	}
}

static void apply(const struct wf_sysconf *c)
{
	char host[WF_HOSTNAME_MAX + 1];

#if !defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	if (net_if_get_default() != NULL) {
		use_device_mac(net_if_get_default());
	}
#endif

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
#if defined(CONFIG_DNS_RESOLVER)
				/* feste Adresse: Namen (Zeitserver) ueber das Gateway aufloesen */
				char gws[16];
				const char *servers[] = {gws, NULL};

				wf_ipv4_format(c->gateway, gws);
				(void)dns_resolve_reconfigure(dns_resolve_get_default(), servers, NULL,
							      DNS_SOURCE_MANUAL);
#endif
			}
		}
	}
#endif
	if (!c->telnet) {
		(void)shell_stop(shell_backend_telnet_get_ptr());
		k_work_reschedule(&telnet_close_work, K_NO_WAIT);
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

void netcfg_ipv4(uint32_t *ip, uint32_t *mask)
{
	*ip = 0;
	*mask = 0;
#if !defined(CONFIG_NET_NATIVE_OFFLOADED_SOCKETS)
	struct net_if *iface = net_if_get_default();
	struct net_in_addr *a = NULL;

	if (iface != NULL) {
		a = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);
	}
	if (a != NULL) {
		struct net_in_addr m = net_if_ipv4_get_netmask_by_addr(iface, a);

		/* s_addr liegt in Netzbyteordnung im Speicher: als uint32_t auf dem
		 * Cortex-M3 (little-endian) ist das erste Oktett das niederwertigste
		 * Byte, wie lwIP im Bootloader es erwartet */
		*ip = a->s_addr;
		*mask = m.s_addr;
	}
#endif
}

void netcfg_bootloader_addr(char out[16], uint32_t *ip, uint32_t *mask)
{
	netcfg_ipv4(ip, mask);
	uint32_t a = *ip != 0 ? *ip : 0x0800A8C0u; /* 192.168.0.8 des Bootloaders */

	snprintf(out, 16, "%u.%u.%u.%u", a & 0xFF, (a >> 8) & 0xFF, (a >> 16) & 0xFF, a >> 24);
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
		timesync_notify();
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
		wf_jb_raw(out, "{\"error\":\"ungültiges JSON (erwartet: flaches Objekt)\"}");
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
