/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Schuetzueberwachung (Abschnitt 7.4, SOLL-PROT-20..23, Feld 390).
 */
#ifndef WALLFREE_EVSE_CTM_H_
#define WALLFREE_EVSE_CTM_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evse_ctm_mode {
	EVSE_CTM_OFF = 0,
	EVSE_CTM_SENSE = 1,   /* Klemme CT, PE0 */
	EVSE_CTM_NO = 2,      /* Eingang Code 5 */
	EVSE_CTM_NC = 3,      /* Eingang Code 6 */
	EVSE_CTM_METER = 4,   /* nur mit Messquelle (F1) */
};

#define EVSE_CTM_CHECK_DELAY_MS 200
#define EVSE_CTM_FAULT_MS       250
#define EVSE_CTM_SENSE_EDGE_MS  100
#define EVSE_CTM_PHASE_V        50
#define EVSE_CTM_ON_DELAY_MS    1000 /* SOLL-PROT-25 */
#define EVSE_CTM_ON_FAULT_MS    1000

struct evse_ctm_in {
	bool contactor_on;          /**< Soll-Zustand */
	evse_ms_t off_since;        /**< Zeitpunkt des Aus-Befehls */
	evse_ms_t on_since;         /**< Zeitpunkt des Ein-Befehls */
	evse_ms_t last_sense_edge;  /**< letzte steigende Flanke an PE0 */
	bool ct_no;                 /**< Eingang Code 5 aktiv */
	bool ct_nc;                 /**< Eingang Code 6 aktiv */
	bool meter_valid;
	int32_t phase_v[3];
};

struct evse_ctm {
	evse_ms_t bad_since;
	bool bad;     /**< Erkennung aktuell */
	bool fault;   /**< ERR-CONTACTOR, verriegelnd (F14) */
	bool fault_on; /**< Fehler im Ein erkannt: zog nicht an (SOLL-PROT-25) */
	bool sense;   /**< Spannung an CT (Flanke an PE0 juenger als 100 ms), in jedem Modus; Anzeige */
};

/** Modus zulaessig? Modus 4 nur mit Messquelle (SOLL-PROT-22). */
bool evse_ctm_mode_valid(int mode, bool meter_available);

void evse_ctm_init(struct evse_ctm *c);

void evse_ctm_update(struct evse_ctm *c, enum evse_ctm_mode mode, const struct evse_ctm_in *in,
		     evse_ms_t now);

/** Quittierung: loescht den Fehler nur, wenn er nicht mehr erkannt wird. */
bool evse_ctm_acknowledge(struct evse_ctm *c);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_CTM_H_ */
