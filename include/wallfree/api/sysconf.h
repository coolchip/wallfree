/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter der Anwendung (Soll-Spezifikation 10.1: net_*,
 * telnet_enable, meter_*; F20, F25, F28). Reines C ohne Netzwerk: Pruefung, JSON,
 * Kodierung fuer den Parameterspeicher (Kennungen 3010..), Import aus dem
 * Originalbereich. Aenderungen wirken nach einem Neustart.
 */
#ifndef WALLFREE_API_SYSCONF_H_
#define WALLFREE_API_SYSCONF_H_

#include <stdbool.h>
#include <stdint.h>
#include <wallfree/api/json.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_HOSTNAME_MAX 32

struct wf_sysconf {
	bool dhcp;
	uint32_t ip;        /**< a.b.c.d als (a << 24) | ... */
	uint32_t netmask;
	uint32_t gateway;   /**< 0 = kein Gateway */
	char hostname[WF_HOSTNAME_MAX + 1]; /**< "" = automatisch wallfree-<MAC-Ende> */
	bool telnet;
	uint8_t meter_type;     /**< enum meter_type, 0 = kein Zaehler */
	uint8_t meter_addr;     /**< 1..247 */
	uint32_t meter_baud;
	uint8_t meter_framing;  /**< enum wf_framing */
};

/** meter_framing */
enum wf_framing {
	WF_FRAMING_8N1 = 0,
	WF_FRAMING_8E1,
	WF_FRAMING_8O1,
	WF_FRAMING_8N2,
};

enum wf_sys_type {
	WF_SYS_BOOL = 0,
	WF_SYS_IPV4,
	WF_SYS_HOST,
	WF_SYS_CODE,  /**< Zahl aus einer Liste bzw. einem Bereich */
};

struct wf_syspar {
	const char *name;
	uint16_t id;          /**< Kennung im Parameterspeicher */
	uint8_t type;         /**< enum wf_sys_type */
	const char *help;
};

#define WF_SYS_ID_FIRST 3010

void wf_sysconf_defaults(struct wf_sysconf *c);
size_t wf_sys_count(void);
const struct wf_syspar *wf_sys_at(size_t i);
const struct wf_syspar *wf_sys_find(const char *name);
const struct wf_syspar *wf_sys_by_id(uint16_t id);

/** Einen Wert setzen (Text-IP, true/false bzw. 0/1, Text). @return NULL oder Fehlertext */
const char *wf_sys_set(struct wf_sysconf *c, const struct wf_syspar *p, const struct wf_jval *v);

/** Gesamtpruefung (z. B. Gateway im Netz der Adresse). @return NULL oder Fehlertext */
const char *wf_sys_check(const struct wf_sysconf *c);

/** {"net_dhcp": true, "net_ip": "192.168.0.8", ...} */
void wf_sys_json(const struct wf_sysconf *c, struct wf_jbuf *b);

/** Beschreibung aller Systemparameter als Liste. */
void wf_sys_info_json(struct wf_jbuf *b);

/** Fuer den Speicher kodieren. @return Laenge */
size_t wf_sys_encode(const struct wf_sysconf *c, const struct wf_syspar *p, uint8_t *buf);

/** Gespeicherten Wert uebernehmen. @return 0 oder -1 (ungueltig, Default bleibt) */
int wf_sys_decode(struct wf_sysconf *c, uint16_t id, const void *data, size_t len);

/** IPv4 lesen ("a.b.c.d", jede Zahl 0..255 ohne fuehrende Nullen). @return 0 oder -1 */
int wf_ipv4_parse(const char *s, uint32_t *out);
void wf_ipv4_format(uint32_t ip, char out[16]);

/** Lesezugriff auf den Originalbereich: @return 0 gefunden, sonst != 0 */
typedef int (*wf_orig_lookup_t)(void *ctx, uint16_t field, uint16_t *value);

/**
 * Netzwerkwerte aus dem Original uebernehmen (Feld 404 DHCP, 315..318 IP,
 * 319..322 Netzmaske, 323..326 Gateway, Belegung laut Original-Webseite
 * lan.html). Felder ohne Eintrag behalten den bisherigen Wert.
 * @return Anzahl uebernommener Werte oder -1, wenn das Ergebnis ungueltig
 *         waere (dann bleibt @p c unveraendert; Klartext in @p err)
 */
int wf_sys_import_orig(struct wf_sysconf *c, wf_orig_lookup_t lookup, void *ctx,
		       const char **err);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_SYSCONF_H_ */
