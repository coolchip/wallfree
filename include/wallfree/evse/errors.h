/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Fehlermodell (Abschnitt 11.1). Klassen:
 *   P  Schutzfehler: Schuetz sofort aus, Ein-Sperre
 *   K  Konfigurationsfehler: kein Laden bis Korrektur/Neustart
 *   VI Fahrzeug-/Ablauffehler mit Sofortabschaltung
 *   VO Fahrzeug-/Ablauffehler mit geordnetem Ende
 */
#ifndef WALLFREE_EVSE_ERRORS_H_
#define WALLFREE_EVSE_ERRORS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t evse_err_t;

#define EVSE_ERR_CABLE_REJECT  (1u << 0)  /* R107.0/.1, VO */
#define EVSE_ERR_PP_INVALID    (1u << 1)  /* R107.2,  VI */
#define EVSE_ERR_CP_INVALID    (1u << 2)  /* R107.3,  VI */
#define EVSE_ERR_LOCK          (1u << 3)  /* R107.5,  Ablauf Verriegelung */
#define EVSE_ERR_UNLOCK        (1u << 4)  /* R107.6,  Ablauf Verriegelung */
#define EVSE_ERR_LD_LOST       (1u << 5)  /* R107.7,  VI (SOLL-LOCK-40) */
#define EVSE_ERR_OVERCURRENT   (1u << 6)  /* R107.8,  VI */
#define EVSE_ERR_METER_COMM    (1u << 7)  /* R107.9,  VO */
#define EVSE_ERR_STATE_D       (1u << 8)  /* R107.10, VI */
#define EVSE_ERR_CONTACTOR     (1u << 9)  /* R107.11, P */
#define EVSE_ERR_DIODE         (1u << 10) /* R107.12, VI */
#define EVSE_ERR_INPUT         (1u << 11) /* R155.1,  VI */
#define EVSE_ERR_RCM_SELFTEST  (1u << 12) /* R155.3,  P */
#define EVSE_ERR_RCM_SENSOR    (1u << 13) /* R155.4,  P */
#define EVSE_ERR_RCM_DC6       (1u << 14) /* R155.5,  P */
#define EVSE_ERR_RCM_AC30      (1u << 15) /* R155.6,  P */
#define EVSE_ERR_SWITCH        (1u << 16) /* neu E11, K */
#define EVSE_ERR_CONFIG        (1u << 17) /* neu, K */
#define EVSE_ERR_INTERNAL      (1u << 18) /* neu E3/E5, P */

#define EVSE_ERR_RCM_ANY (EVSE_ERR_RCM_SELFTEST | EVSE_ERR_RCM_SENSOR | EVSE_ERR_RCM_DC6 | \
			  EVSE_ERR_RCM_AC30)
#define EVSE_ERR_CLASS_P (EVSE_ERR_CONTACTOR | EVSE_ERR_RCM_ANY | EVSE_ERR_INTERNAL)
#define EVSE_ERR_CLASS_K (EVSE_ERR_SWITCH | EVSE_ERR_CONFIG)
#define EVSE_ERR_CLASS_VI (EVSE_ERR_PP_INVALID | EVSE_ERR_CP_INVALID | EVSE_ERR_OVERCURRENT | \
			   EVSE_ERR_STATE_D | EVSE_ERR_DIODE | EVSE_ERR_INPUT)
#define EVSE_ERR_CLASS_VO (EVSE_ERR_CABLE_REJECT | EVSE_ERR_METER_COMM)

/** Fehler, die bei CP A (Fahrzeug getrennt) geloescht werden (SOLL-ERR-02). */
#define EVSE_ERR_RESET_AT_A (EVSE_ERR_LOCK | EVSE_ERR_UNLOCK | EVSE_ERR_LD_LOST | \
			     EVSE_ERR_OVERCURRENT | EVSE_ERR_STATE_D | EVSE_ERR_DIODE)

/** Fehler, die den CP-Status nur bei Rohzustand != A auf E zwingen (SOLL-CP-08). */
#define EVSE_ERR_E_NOT_A (EVSE_ERR_LD_LOST | EVSE_ERR_OVERCURRENT | EVSE_ERR_STATE_D | \
			  EVSE_ERR_RCM_ANY | EVSE_ERR_LOCK)

/** Warnungen (keine Wirkung auf den Ablauf). */
#define EVSE_WARN_SUPPLY (1u << 0) /* F16 */

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_ERRORS_H_ */
