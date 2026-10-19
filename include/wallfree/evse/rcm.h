/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fehlerstromueberwachung und RCM-Selbsttest (Abschnitt 7.2/7.3,
 * SOLL-PROT-04..13). Pegel: true = ausgeloest (MCU-Sicht nach
 * Inversionsstufe, SOLL-PROT-06).
 */
#ifndef WALLFREE_EVSE_RCM_H_
#define WALLFREE_EVSE_RCM_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Fehlerbits (SOLL-PROT-07, Tabelle 11.1). */
#define EVSE_RCM_F_SENSOR   (1u << 0) /* ERR_OUT, ERR-RCM-SENSOR */
#define EVSE_RCM_F_DC6      (1u << 1) /* SWITCH_2, ERR-RCM-DC6 */
#define EVSE_RCM_F_AC30     (1u << 2) /* SWITCH_1, ERR-RCM-AC30 */
#define EVSE_RCM_F_SELFTEST (1u << 3) /* ERR-RCM-SELFTEST */

/* Zeitfolge des Selbsttests ab Start (Phoenix 1.27, E16) */
#define EVSE_RCM_T_ERRCHECK   100
#define EVSE_RCM_T_PULSE_ON   200
#define EVSE_RCM_T_PULSE_OFF  300
#define EVSE_RCM_T_SW2        1000
#define EVSE_RCM_T_SW1        1700
#define EVSE_RCM_T_NOT_BOTH   2400
#define EVSE_RCM_T_RELEASED   3400

struct evse_rcm_levels {
	bool sw1; /**< PE3, SWITCH_1, 30 mA rms */
	bool sw2; /**< PE4, SWITCH_2, 6 mA DC / 30 mA AC */
	bool err; /**< PE6, ERR_OUT */
};

enum evse_rcm_test {
	EVSE_RCM_TEST_IDLE = 0,
	EVSE_RCM_TEST_RUNNING,
	EVSE_RCM_TEST_PASSED,
	EVSE_RCM_TEST_FAILED,
	/** Test nach dem Impuls abgebrochen: Sensor laeuft noch, SWITCH_1/2 bis
	 *  EVSE_RCM_T_RELEASED ausgeblendet, ERR_OUT weiter ausgewertet */
	EVSE_RCM_TEST_ABORTING,
};

struct evse_rcm {
	uint8_t faults;             /**< verriegelnde Fehlerbits (SOLL-PROT-08) */
	enum evse_rcm_test test;
	evse_ms_t test_since;
	uint8_t test_step;
	bool out_test;              /**< TEST_IN (PE5) aktiv = Low */
};

void evse_rcm_init(struct evse_rcm *r);

/** Reine Pegelbewertung, auch fuer den ISR-Pfad (SOLL-PROT-04/07). */
uint8_t evse_rcm_check(const struct evse_rcm_levels *lv);

/**
 * Zyklische Ueberwachung ausserhalb des Selbsttests.
 * @return true, wenn ein Signal ausgeloest ist (sofort abschalten).
 */
bool evse_rcm_monitor(struct evse_rcm *r, const struct evse_rcm_levels *lv);

/**
 * Selbsttest starten (nur bei Schuetz aus, Aufrufer prueft das).
 * @return false, wenn die Vorbedingung nicht erfuellt ist (ein Signal
 *         ausgeloest); der Test laeuft dann nicht, Fehler bleiben.
 */
bool evse_rcm_selftest_start(struct evse_rcm *r, const struct evse_rcm_levels *lv, evse_ms_t now);

/** Selbsttest fortschreiben (mindestens alle 10 ms aufrufen). */
void evse_rcm_selftest_update(struct evse_rcm *r, const struct evse_rcm_levels *lv, evse_ms_t now);

/** Selbsttest abbrechen (SOLL-SM-42): gilt als nicht bestanden, kein Fehler.
 *  Nach dem Testimpuls folgt EVSE_RCM_TEST_ABORTING bis zum Ende des Sensorablaufs. */
void evse_rcm_selftest_abort(struct evse_rcm *r);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_RCM_H_ */
