/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Abbildung der Ladelogik auf die HTTP-/WebSocket-API (Soll-Spezifikation
 * 12.1, SOLL-API-03..06/30), WARP-angelehnt. Reines C, ohne Netzwerk; die
 * Anwendung haelt beim Aufruf die Sperre der Ladelogik.
 *
 * Quelle der WARP-Semantik: docs.warp-charger.com, API-Referenz EVSE
 * (gelesen 2026-10-19); evcc charger/warp-ws.go (master b8c43c0).
 */
#ifndef WALLFREE_API_EVSE_API_H_
#define WALLFREE_API_EVSE_API_H_

#include <wallfree/api/json.h>
#include <wallfree/evse/param.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WARP error_state; 1 ist bei WARP unbelegt und steht hier fuer "sonstiger Fehler" */
#define API_ERR_OK        0
#define API_ERR_OTHER     1
#define API_ERR_SWITCH    2 /* ERR-SWITCH, ERR-CONFIG */
#define API_ERR_RCM       3
#define API_ERR_CONTACTOR 4

int api_iec61851_state(const struct evse *e);
int api_charger_state(const struct evse *e);
int api_error_state(const struct evse *e);
int api_contactor_state(const struct evse *e);
int api_contactor_error(const struct evse *e);

/** GET /evse/state */
void api_json_state(const struct evse *e, struct wf_jbuf *b);
/** GET /evse/external_current */
void api_json_external_current(const struct evse *e, struct wf_jbuf *b);

/**
 * Koerper von POST /evse/external_current: {"current": mA} oder WARP-Kurzform
 * "mA". @return 0 oder -1 (Syntax)
 */
int api_parse_current(const char *body, size_t len, int32_t *ma);

/** GET /evse/params: alle Werte als {"name": Wert, ...} */
void api_json_params(const struct evse *e, struct wf_jbuf *b);

/**
 * GET /evse/param_info: Beschreibung aller Parameter als Liste, in Teilen.
 * Beginnt bei @p idx == 0 mit '[' und endet mit ']'.
 * @return naechster Index; == evse_param_count() + 1, wenn fertig
 */
size_t api_json_param_info(size_t idx, struct wf_jbuf *b);

/** Rueckruf nach erfolgreichem Schreiben (z. B. Speichern persistenter Werte). */
typedef void (*api_written_t)(void *ctx, const struct evse_param *p, int32_t v);

/**
 * POST /evse/params mit {"name": Wert, ...}. Jeder Wert wird einzeln mit
 * evse_param_set geprueft und geschrieben (E14); abgelehnte Werte erscheinen
 * mit Klartext in {"errors": {...}}, gueltige bleiben wirksam.
 * @return HTTP-Status 200 (alles uebernommen) oder 400
 */
int api_set_params(struct evse *e, const char *body, size_t len, evse_ms_t now,
		   api_written_t written, void *ctx, struct wf_jbuf *out);

/** Name des Fehlerbits @p bit (z. B. "CP-INVALID"), NULL ausserhalb. */
const char *api_err_name(unsigned int bit);

/** Ruecksetzbedingung des Fehlerbits laut Tabelle 11.1 (Klartext). */
const char *api_err_reset(unsigned int bit);

/** Fehlerbits als JSON-Feld der Namen, z. B. ["RCM-DC6","RCM-AC30"]. */
void api_json_err_list(struct wf_jbuf *b, evse_err_t m);

/**
 * GET /evse/errors (SOLL-ERR-01): {"active": [...], "seen": [...],
 * "history": [{"age_ms": n, "state": n, "set": [...], "cleared": [...]}, ...]},
 * Historie juengste zuerst.
 */
void api_json_errors(const struct evse *e, evse_ms_t now, struct wf_jbuf *b);

/** {"error": "<text>"} */
void api_json_error(struct wf_jbuf *b, const char *text);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_EVSE_API_H_ */
