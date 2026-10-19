/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Netz- und Systemparameter der Anwendung (10.1 net_*, telnet_enable).
 * Gespeicherte Werte wirken nach einem Neustart; bis dahin laeuft die
 * beim Start angewandte Konfiguration weiter.
 */
#ifndef WALLFREE_APP_NETCFG_H_
#define WALLFREE_APP_NETCFG_H_

#include <wallfree/api/sysconf.h>

/** Beim Start nach storage_init(): laden, Hostname/Adresse/Telnet anwenden. */
void netcfg_init(void);

/** Laufende und gespeicherte (beim naechsten Start wirksame) Konfiguration. */
void netcfg_get(struct wf_sysconf *active, struct wf_sysconf *saved);

/** Neustart noetig, damit gespeicherte Werte wirken? */
bool netcfg_restart_required(void);

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
