/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Parameterverwaltung (Abschnitt 10, SOLL-PAR-01..03/40, E14).
 *
 * Eine Tabelle beschreibt alle Konfigurations-, Laufzeit- und Statuswerte
 * der Ladelogik. Jeder Schreibzugriff wird geprueft; unzulaessige Werte
 * werden mit Fehlercode abgelehnt, nie still verworfen. HTTP und Shell
 * greifen nur ueber diese Schicht zu (SOLL-API-20/30).
 */
#ifndef WALLFREE_EVSE_PARAM_H_
#define WALLFREE_EVSE_PARAM_H_

#include <stddef.h>
#include <wallfree/evse/charger.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evse_param_kind {
	EVSE_PK_CONFIG = 0,  /**< Abschnitt 10.1, persistent */
	EVSE_PK_RUNTIME,     /**< Abschnitt 10.2, nicht persistent */
	EVSE_PK_STATUS,      /**< Abschnitt 10.3, nur lesbar */
};

enum evse_param_err {
	EVSE_PARAM_OK = 0,
	EVSE_PARAM_E_UNKNOWN,     /**< kein solcher Parameter */
	EVSE_PARAM_E_READONLY,    /**< nur lesbar */
	EVSE_PARAM_E_RANGE,       /**< Wert ausserhalb des Bereichs */
	EVSE_PARAM_E_STATE,       /**< im aktuellen Zustand nicht erlaubt */
	EVSE_PARAM_E_UNAVAILABLE, /**< Funktion nicht verfuegbar (z. B. ohne Messquelle) */
};

struct evse_param {
	const char *name;
	uint16_t id;          /**< stabile Kennung fuer den Parameterspeicher */
	uint16_t orig_field;  /**< Feldnummer im Original, 0 = neu */
	const char *unit;     /**< "" wenn ohne Einheit */
	uint8_t kind;         /**< enum evse_param_kind */
	int32_t min;
	int32_t max;
	int32_t def;          /**< Werkseinstellung (nur CONFIG/RUNTIME) */
	const char *help;     /**< Kurzbeschreibung */
	/* Zugriff: entweder ueber Offset ... */
	uint8_t base;         /**< 0 = struct evse_config, 1 = struct evse_runtime, 2 = Funktion */
	uint16_t off;
	uint8_t size;         /**< 1, 2 oder 4 Byte */
	bool is_signed;
	/* ... und/oder ueber Funktionen */
	int32_t (*get)(const struct evse *e);
	int (*check)(const struct evse_config *cfg, int32_t v); /**< reine Wertpruefung */
	int (*set)(struct evse *e, int32_t v, evse_ms_t now);
};

size_t evse_param_count(void);
const struct evse_param *evse_param_at(size_t i);
const struct evse_param *evse_param_find(const char *name);
const struct evse_param *evse_param_by_id(uint16_t id);

/** Wert lesen. */
int32_t evse_param_get(const struct evse *e, const struct evse_param *p);

/**
 * Wert schreiben mit Grenz- und Zustandspruefung.
 * @return EVSE_PARAM_OK oder Fehlercode; bei Fehler bleibt alles unveraendert.
 */
int evse_param_set(struct evse *e, const struct evse_param *p, int32_t v, evse_ms_t now);

/** Schreiben per Name (fuer Shell/HTTP). */
int evse_param_set_by_name(struct evse *e, const char *name, int32_t v, evse_ms_t now);

/** Persistent zu speichern? (nur CONFIG) */
static inline bool evse_param_persistent(const struct evse_param *p)
{
	return p->kind == EVSE_PK_CONFIG;
}

/** Klartext zu einem Fehlercode. */
const char *evse_param_strerror(int err);

/**
 * Gespeicherten Wert beim Start uebernehmen (SOLL-BOOT-04): wie
 * evse_param_set, aber ohne Zustandspruefung; ungueltige Werte werden
 * abgelehnt und behalten den Default.
 */
int evse_param_restore(struct evse_config *cfg, uint16_t id, int32_t v);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_PARAM_H_ */
