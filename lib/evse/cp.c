/* SPDX-License-Identifier: Apache-2.0 */
/* CP-Auswertung, SOLL-CP-01..07 */
#include <wallfree/evse/cp.h>

int evse_cp_band(int32_t v, enum evse_cp_state prev)
{
	/* SOLL-CP-01; Grenzwert gehoert zum zuerst genannten Band */
	if (v >= 11000) {
		return EVSE_CP_A;
	}
	if (v >= 10000) {
		return prev == EVSE_CP_A ? EVSE_CP_A : EVSE_CP_B;
	}
	if (v >= 8000) {
		return EVSE_CP_B;
	}
	if (v >= 7000) {
		return prev <= EVSE_CP_B ? EVSE_CP_B : EVSE_CP_C;
	}
	if (v >= 5000) {
		return EVSE_CP_C;
	}
	if (v >= 4000) {
		return prev <= EVSE_CP_C ? EVSE_CP_C : EVSE_CP_D;
	}
	if (v >= 2000) {
		return EVSE_CP_D;
	}
	if (v >= EVSE_CP_INVALID_MV) {
		return prev <= EVSE_CP_D ? EVSE_CP_D : EVSE_CP_E;
	}
	return -1;
}

void evse_cp_init(struct evse_cp *cp)
{
	evse_debounce_init(&cp->raw, EVSE_CP_A);
	cp->invalid_since = EVSE_MS_NONE;
	cp->invalid = false;
	cp->diode_since = EVSE_MS_NONE;
	cp->diode_fault = false;
	cp->discard = EVSE_CP_DISCARD_BLOCKS;
	cp->last_pos_mv = 0;
	cp->last_neg_mv = 0;
}

void evse_cp_output_changed(struct evse_cp *cp)
{
	cp->discard = EVSE_CP_DISCARD_BLOCKS;
	/* Fristen, die auf Messwerten der alten Ausgabe beruhen, neu beginnen */
	cp->raw.dev_since = EVSE_MS_NONE;
	cp->diode_since = EVSE_MS_NONE;
}

void evse_cp_clear_diode(struct evse_cp *cp)
{
	cp->diode_fault = false;
	cp->diode_since = EVSE_MS_NONE;
}

static void update_level(struct evse_cp *cp, int32_t v, evse_ms_t now)
{
	int cand = evse_cp_band(v, evse_cp_raw(cp));

	if (cand < 0) {
		/*
		 * SOLL-CP-06: Kandidat "ungueltig". Der Rohzustand bleibt
		 * unveraendert (Abweichung vom Original, das hier A annimmt;
		 * ein Kurzschluss darf nicht als "Fahrzeug getrennt" gelten).
		 */
		if (cp->invalid_since == EVSE_MS_NONE) {
			cp->invalid_since = now;
		}
		cp->invalid = evse_elapsed_gt(now, cp->invalid_since, EVSE_CP_INVALID_MS);
		cp->raw.dev_since = EVSE_MS_NONE;
		return;
	}

	cp->invalid = false;
	if (v >= EVSE_CP_INVALID_RESET_MV) {
		cp->invalid_since = EVSE_MS_NONE;
	}
	(void)evse_debounce_update(&cp->raw, cand, now, EVSE_CP_DEBOUNCE_MS);
}

static void update_diode(struct evse_cp *cp, const struct evse_cp_sample *s, evse_ms_t now)
{
	/*
	 * Fehlende Diode nur bei gueltigem positivem Plateau: Ein Kurzschluss
	 * CP-PE senkt beide Plateaus und ist CP ungueltig (SOLL-CP-06), sonst
	 * gewinnt je nach Abtastung die Diodenpruefung (Testgeraet, Stufe C).
	 */
	bool check = s->neg_valid && s->pwm_active && !s->current_blocked &&
		     s->pos_valid && s->pos_mv >= EVSE_CP_DIODE_POS_MV &&
		     evse_elapsed_gt(now, s->pwm_since, EVSE_CP_DIODE_PWM_MS);

	if (!check || s->neg_mv >= EVSE_CP_DIODE_MV) {
		cp->diode_since = EVSE_MS_NONE;
		return;
	}
	if (cp->diode_since == EVSE_MS_NONE) {
		cp->diode_since = now;
	}
	if (evse_elapsed_gt(now, cp->diode_since, EVSE_CP_DIODE_MS)) {
		cp->diode_fault = true; /* verriegelnd bis CP A */
	}
}

void evse_cp_update(struct evse_cp *cp, const struct evse_cp_sample *s, evse_ms_t now)
{
	if (cp->discard > 0) {
		cp->discard--;
		return;
	}
	cp->last_pos_mv = s->pos_mv;
	cp->last_neg_mv = s->neg_mv;

	/* SOLL-CP-04: nur tatsaechlich ausgegebene Plateaus auswerten */
	if (s->pos_valid) {
		update_level(cp, s->pos_mv, now);
	}
	update_diode(cp, s, now);
}
