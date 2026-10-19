/* SPDX-License-Identifier: Apache-2.0 */
/*
 * CP-Auswertung (Soll-Spezifikation Abschnitt 4.1, SOLL-CP-01..07).
 *
 * Eingang je Messblock (~8 ms): Pegel des positiven Plateaus und Betrag des
 * negativen Plateaus in "mV", dazu, welche Plateaus die aktuelle Ausgabe
 * ueberhaupt erzeugt. Ergebnis: entprellter CP-Rohzustand (nur Pegel),
 * "CP ungueltig" und "Diode fehlt". Die Erzwingung des nach aussen
 * gemeldeten Status durch Fehler (SOLL-CP-08) macht der Ladeautomat.
 */
#ifndef WALLFREE_EVSE_CP_H_
#define WALLFREE_EVSE_CP_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evse_cp_state {
	EVSE_CP_A = 0,
	EVSE_CP_B,
	EVSE_CP_C,
	EVSE_CP_D,
	EVSE_CP_E,
	EVSE_CP_F,
};

/** Fristen und Schwellen (SOLL-CP-01/03/06/07). */
#define EVSE_CP_DEBOUNCE_MS     20
#define EVSE_CP_INVALID_MS      20
#define EVSE_CP_INVALID_MV      1000  /* darunter Kandidat "ungueltig" */
#define EVSE_CP_INVALID_RESET_MV 1200 /* ab hier Frist zuruecksetzen */
#define EVSE_CP_DIODE_MV        10000 /* Betrag CP- muss >= sein */
#define EVSE_CP_DIODE_MS        20
#define EVSE_CP_DIODE_PWM_MS    100   /* PWM muss so lange laufen */
#define EVSE_CP_DISCARD_BLOCKS  2     /* SOLL-CP-05 */

/** Ein Messblock. */
struct evse_cp_sample {
	int32_t pos_mv;     /**< positives Plateau */
	int32_t neg_mv;     /**< Betrag des negativen Plateaus */
	bool pos_valid;     /**< Ausgabe erzeugt ein positives Plateau */
	bool neg_valid;     /**< Ausgabe erzeugt ein negatives Plateau */
	bool pwm_active;    /**< PWM mit Tastgrad 3..97 % */
	evse_ms_t pwm_since; /**< seit wann diese PWM laeuft */
	bool current_blocked; /**< Stromsperre aktiv (keine Diodenpruefung) */
};

struct evse_cp {
	struct evse_debounce raw;   /**< Rohzustand (enum evse_cp_state) */
	evse_ms_t invalid_since;
	bool invalid;               /**< ERR-CP-INVALID */
	evse_ms_t diode_since;
	bool diode_fault;           /**< ERR-DIODE, verriegelnd */
	uint8_t discard;            /**< noch zu verwerfende Bloecke */
	int32_t last_pos_mv;
	int32_t last_neg_mv;
};

void evse_cp_init(struct evse_cp *cp);

/** Die CP-Ausgabe hat sich geaendert: naechste Bloecke verwerfen (SOLL-CP-05). */
void evse_cp_output_changed(struct evse_cp *cp);

/** Einen Messblock auswerten. */
void evse_cp_update(struct evse_cp *cp, const struct evse_cp_sample *s, evse_ms_t now);

/** Diodenfehler loeschen (vom Automaten bei CP A, SOLL-CP-07). */
void evse_cp_clear_diode(struct evse_cp *cp);

static inline enum evse_cp_state evse_cp_raw(const struct evse_cp *cp)
{
	return (enum evse_cp_state)cp->raw.value;
}

/**
 * Pegelkandidat nach der Bandtabelle SOLL-CP-01 mit Hysterese gegen den
 * bisherigen Rohzustand (SOLL-CP-02). @return -1 fuer "ungueltig" (< 1,0 V).
 */
int evse_cp_band(int32_t pos_mv, enum evse_cp_state prev_raw);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_CP_H_ */
