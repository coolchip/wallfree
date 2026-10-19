/* SPDX-License-Identifier: Apache-2.0 */
/*
 * PP-Auswertung (Proximity, Klemme PX), SOLL-PP-01..06.
 */
#ifndef WALLFREE_EVSE_PP_H_
#define WALLFREE_EVSE_PP_H_

#include <wallfree/evse/common.h>
#include <wallfree/evse/cp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVSE_PP_HOLD_MS 1000

/** Kabelwert in A; -1 = ungueltig (< 75 Ohm), 0 = kein Kabel. */
#define EVSE_PP_INVALID (-1)

struct evse_pp_config {
	bool d1_proximity;   /**< DIP D1: PP auswerten (Ladefall B) */
	bool d2_reject;      /**< DIP D2: Kabel abweisen */
	bool d3_only13;      /**< DIP D3: nur 13 A abweisen */
	int16_t rotary_a;    /**< Drehschalterwert in A (Ladefall C), 0 wenn ungueltig */
};

struct evse_pp {
	int16_t cable_a;       /**< uebernommener Kabelwert */
	int16_t candidate;
	evse_ms_t cand_since;
	bool invalid;          /**< ERR-PP-INVALID */
	bool reject;           /**< ERR-CABLE-REJECT */
	bool unplugged_event;  /**< in diesem Aufruf Kabelwert 0 uebernommen (SOLL-PP-06) */
};

void evse_pp_init(struct evse_pp *pp);

/** Kandidat aus dem Pegel (SOLL-PP-02, Original-Schwellen). */
int16_t evse_pp_classify(int32_t pp_mv);

/**
 * Einen Messblock auswerten.
 * @param cp_raw aktueller CP-Rohzustand (fuer "kein Kabel bei Fahrzeug")
 */
void evse_pp_update(struct evse_pp *pp, const struct evse_pp_config *cfg, int32_t pp_mv,
		    enum evse_cp_state cp_raw, evse_ms_t now);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_PP_H_ */
