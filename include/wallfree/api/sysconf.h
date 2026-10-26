/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter der Anwendung (Soll-Spezifikation 10.1: net_*,
 * telnet_enable, meter_*, mqtt_*; F20, F25, F28, F30). Reines C ohne Netzwerk:
 * Pruefung, JSON, Kodierung fuer den Parameterspeicher (Kennungen 3010..),
 * Import aus dem Originalbereich. Aenderungen wirken nach einem Neustart,
 * mqtt_* sofort (SOLL-MQTT-31, wf_sys_take_live).
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
#define WF_MQTT_USER_MAX 32
#define WF_MQTT_PW_MAX 64
#define WF_MQTT_PREFIX_MAX 64
#define WF_NTP_SERVER_MAX 64
#define WF_SYS_VAL_MAX 64 /**< groesster kodierter Wert (NVM_MAX_LEN) */

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
	bool mqtt_enable;
	bool mqtt_commands;     /**< Befehle annehmen (SOLL-MQTT-20) */
	uint16_t mqtt_port;
	uint16_t mqtt_interval; /**< s, 0 = ohne Bremse */
	uint32_t mqtt_broker;   /**< 0 = keiner */
	char mqtt_user[WF_MQTT_USER_MAX + 1];
	char mqtt_password[WF_MQTT_PW_MAX + 1]; /**< nie ausgegeben */
	char mqtt_prefix[WF_MQTT_PREFIX_MAX + 1]; /**< "" = wallfree/<MAC-Ende> */
	bool ntp_enable;        /**< Uhrzeit per SNTP holen */
	char ntp_server[WF_NTP_SERVER_MAX + 1]; /**< Name oder IPv4, optional :Port; "" = automatisch */
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
	WF_SYS_TEXT,
	WF_SYS_SECRET, /**< Text, wird nie ausgegeben (JSON null) */
};

struct wf_syspar {
	const char *name;
	uint16_t id;          /**< Kennung im Parameterspeicher */
	uint8_t type;         /**< enum wf_sys_type */
	const char *help;
};

#define WF_SYS_ID_FIRST 3010
#define WF_SYS_ID_LIVE  3020 /**< ab hier sofort wirksam (mqtt_*, wf_sys_take_live) */

void wf_sysconf_defaults(struct wf_sysconf *c);
size_t wf_sys_count(void);
const struct wf_syspar *wf_sys_at(size_t i);
const struct wf_syspar *wf_sys_find(const char *name);
const struct wf_syspar *wf_sys_by_id(uint16_t id);

/** Einen Wert setzen (Text-IP, true/false bzw. 0/1, Text). @return NULL oder Fehlertext */
const char *wf_sys_set(struct wf_sysconf *c, const struct wf_syspar *p, const struct wf_jval *v);

/** Gesamtpruefung (z. B. Gateway im Netz der Adresse). @return NULL oder Fehlertext */
const char *wf_sys_check(const struct wf_sysconf *c);

/**
 * Gespeicherten, widerspruechlichen Satz beim Start nutzbar machen: feste
 * Adresse ungueltig -> DHCP, MQTT ohne Broker -> aus.
 * @return NULL oder Beschreibung der Korrektur
 */
const char *wf_sys_repair(struct wf_sysconf *c);

/** Sofort wirksame Werte (mqtt_*) von @p src nach @p dst uebernehmen. */
void wf_sys_take_live(struct wf_sysconf *dst, const struct wf_sysconf *src);

/** {"net_dhcp": true, "net_ip": "192.168.0.8", ...} */
void wf_sys_json(const struct wf_sysconf *c, struct wf_jbuf *b);

/**
 * Beschreibung aller Systemparameter als Liste, in Teilen wie api_json_param_info:
 * beginnt bei @p idx == 0 mit '[' und endet mit ']'.
 * @return naechster Index; == wf_sys_count() + 1, wenn fertig
 */
size_t wf_sys_info_json(size_t idx, struct wf_jbuf *b);

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
 * lan.html). Ein fehlender Eintrag gilt als 0 (das Original speichert nur Abweichungen von 0);
 * fehlen alle vier Oktette einer Adresse, bleibt der bisherige Wert.
 * @return Anzahl uebernommener Werte oder -1, wenn das Ergebnis ungueltig
 *         waere (dann bleibt @p c unveraendert; Klartext in @p err)
 */
int wf_sys_import_orig(struct wf_sysconf *c, wf_orig_lookup_t lookup, void *ctx,
		       const char **err);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_SYSCONF_H_ */
