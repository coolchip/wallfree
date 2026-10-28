/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter der Anwendung (10.1 net_*, meter_*, mqtt_*,
 * ntp_*, mcu_temp_offset). Gespeicherte Werte wirken nach einem Neustart; bis dahin
 * laeuft die beim Start angewandte Konfiguration weiter. Ausnahme mqtt_*:
 * sofort (SOLL-MQTT-31).
 */
#ifndef WALLFREE_APP_NETCFG_H_
#define WALLFREE_APP_NETCFG_H_

#include <wallfree/api/sysconf.h>

/** Beim Start nach storage_init(): laden, Hostname und Adresse anwenden. */
void netcfg_init(void);

/** Letzte 3 Byte der MAC als 6 Hexziffern (uid, Hostname, MQTT-Praefix). */
void netcfg_uid(char out[7]);

/** MAC-Adresse der Schnittstelle als "aa:bb:cc:dd:ee:ff", leer ohne Schnittstelle. */
void netcfg_mac(char out[18]);

/** Laufende und gespeicherte (beim naechsten Start wirksame) Konfiguration. */
void netcfg_get(struct wf_sysconf *active, struct wf_sysconf *saved);

/** Neustart noetig, damit gespeicherte Werte wirken? */
bool netcfg_restart_required(void);

/**
 * Aktuelle IPv4-Adresse und Maske, wie sie im Speicher stehen (erstes Oktett im
 * niederwertigsten Byte); 0, wenn keine Adresse (SOLL-BOOT-31).
 */
void netcfg_ipv4(uint32_t *ip, uint32_t *mask);

/**
 * Adresse, unter der der Bootloader nach der Uebergabe antwortet: die aktuelle
 * oder ohne Adresse 192.168.0.8 (SOLL-BOOT-31). Liefert auch @p ip / @p mask fuer
 * die Uebergabe.
 */
void netcfg_bootloader_addr(char out[16], uint32_t *ip, uint32_t *mask);

/**
 * Mehrere Werte als JSON-Objekt setzen (atomar: alle gueltig oder keiner).
 * @return HTTP-Status 200/400; Antwort in @p out
 */
int netcfg_set_json(const char *body, size_t len, struct wf_jbuf *out);

/** Einen Wert setzen (Shell); Text wie in der Shell eingegeben. @return NULL oder Fehlertext */
const char *netcfg_set_text(const char *name, const char *text);

/**
 * Netzwerkwerte aus dem Originalbereich uebernehmen und speichern.
 * @return Anzahl oder -1 (Text in @p err)
 */
int netcfg_import_orig(wf_orig_lookup_t lookup, void *ctx, const char **err);

#endif
