/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-CP-01..07 */
#include <zephyr/ztest.h>
#include <wallfree/evse/cp.h>

#define BLOCK_MS 8

static struct evse_cp cp;
static evse_ms_t now;

static struct evse_cp_sample dc_plus(int32_t pos)
{
	return (struct evse_cp_sample){.pos_mv = pos, .neg_mv = 0, .pos_valid = true};
}

/* Bloecke mit gleichem Messwert fuer eine Dauer einspeisen */
static void feed(struct evse_cp_sample s, int dur_ms)
{
	for (int t = 0; t < dur_ms; t += BLOCK_MS) {
		now += BLOCK_MS;
		evse_cp_update(&cp, &s, now);
	}
}

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_cp_init(&cp);
	now = 0;
	cp.discard = 0;
}

ZTEST(evse_cp, test_band_limits)
{
	/* SOLL-CP-01: jeder Grenzwert und Nachbar */
	zassert_equal(evse_cp_band(14000, EVSE_CP_B), EVSE_CP_A);
	zassert_equal(evse_cp_band(11000, EVSE_CP_B), EVSE_CP_A);
	zassert_equal(evse_cp_band(10999, EVSE_CP_A), EVSE_CP_A);
	zassert_equal(evse_cp_band(10999, EVSE_CP_B), EVSE_CP_B);
	zassert_equal(evse_cp_band(10000, EVSE_CP_C), EVSE_CP_B);
	zassert_equal(evse_cp_band(9999, EVSE_CP_A), EVSE_CP_B);
	zassert_equal(evse_cp_band(8000, EVSE_CP_D), EVSE_CP_B);
	zassert_equal(evse_cp_band(7999, EVSE_CP_A), EVSE_CP_B);
	zassert_equal(evse_cp_band(7999, EVSE_CP_B), EVSE_CP_B);
	zassert_equal(evse_cp_band(7000, EVSE_CP_C), EVSE_CP_C);
	zassert_equal(evse_cp_band(6999, EVSE_CP_A), EVSE_CP_C);
	zassert_equal(evse_cp_band(5000, EVSE_CP_A), EVSE_CP_C);
	zassert_equal(evse_cp_band(4999, EVSE_CP_C), EVSE_CP_C);
	zassert_equal(evse_cp_band(4000, EVSE_CP_D), EVSE_CP_D);
	zassert_equal(evse_cp_band(3999, EVSE_CP_A), EVSE_CP_D);
	zassert_equal(evse_cp_band(2000, EVSE_CP_A), EVSE_CP_D);
	zassert_equal(evse_cp_band(1999, EVSE_CP_D), EVSE_CP_D);
	zassert_equal(evse_cp_band(1000, EVSE_CP_E), EVSE_CP_E);
	zassert_equal(evse_cp_band(999, EVSE_CP_A), -1);
}

ZTEST(evse_cp, test_debounce_20ms)
{
	/* SOLL-CP-03: Uebernahme erst nach > 20 ms Abweichung (3. Block nach Beginn) */
	struct evse_cp_sample b = dc_plus(9000);

	now += BLOCK_MS;
	evse_cp_update(&cp, &b, now); /* t=8, Beginn */
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_A);
	now += BLOCK_MS;
	evse_cp_update(&cp, &b, now); /* 8 ms */
	now += BLOCK_MS;
	evse_cp_update(&cp, &b, now); /* 16 ms */
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_A);
	now += BLOCK_MS;
	evse_cp_update(&cp, &b, now); /* 24 ms > 20 */
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_B);
}

ZTEST(evse_cp, test_short_glitch_ignored)
{
	feed(dc_plus(9000), 16);
	feed(dc_plus(12000), 8);
	feed(dc_plus(9000), 16);
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_A);
}

ZTEST(evse_cp, test_takes_current_candidate)
{
	/* B -> C waehrend der Frist: Abweichung durchgehend, uebernommen wird C */
	feed(dc_plus(9000), 16);
	feed(dc_plus(6000), 16);
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_C);
}

ZTEST(evse_cp, test_invalid_short)
{
	/* SOLL-CP-06 */
	feed(dc_plus(9000), 40);
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_B);
	feed(dc_plus(500), 16);
	zassert_false(cp.invalid);
	feed(dc_plus(500), 16);
	zassert_true(cp.invalid);
	/* Rohzustand bleibt erhalten (kein falsches "A") */
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_B);
	/* 1,0..1,2 V: kein Fehler, Startzeit bleibt */
	feed(dc_plus(1100), 8);
	zassert_false(cp.invalid);
	feed(dc_plus(500), 8);
	zassert_true(cp.invalid, "Frist darf zwischen 1,0 und 1,2 V nicht neu beginnen");
	feed(dc_plus(1300), 8);
	feed(dc_plus(500), 8);
	zassert_false(cp.invalid, "ab 1,2 V beginnt die Frist neu");
}

ZTEST(evse_cp, test_no_pos_plateau)
{
	/* SOLL-CP-04: bei -12 V DC kein positives Plateau, kein "ungueltig" */
	struct evse_cp_sample s = {.pos_mv = 0, .neg_mv = 12000, .neg_valid = true};

	feed(dc_plus(9000), 40);
	feed(s, 200);
	zassert_false(cp.invalid);
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_B);
}

ZTEST(evse_cp, test_discard_after_output_change)
{
	/* SOLL-CP-05 */
	feed(dc_plus(9000), 40);
	evse_cp_output_changed(&cp);
	now += BLOCK_MS;
	struct evse_cp_sample s = dc_plus(500);

	evse_cp_update(&cp, &s, now);
	evse_cp_update(&cp, &s, now + 1);
	zassert_equal(cp.discard, 0);
	zassert_equal(cp.invalid_since, EVSE_MS_NONE, "verworfene Bloecke duerfen nicht zaehlen");
}

ZTEST(evse_cp, test_diode)
{
	/* SOLL-CP-07 */
	struct evse_cp_sample s = {
		.pos_mv = 9000, .neg_mv = 5000, .pos_valid = true, .neg_valid = true,
		.pwm_active = true, .pwm_since = 0, .current_blocked = false,
	};

	now = 0;
	feed(s, 96); /* PWM < 100 ms + Frist: noch nicht */
	zassert_false(cp.diode_fault);
	feed(s, 24);
	zassert_false(cp.diode_fault);
	feed(s, 16);
	zassert_true(cp.diode_fault);
	/* verriegelnd */
	s.neg_mv = 12000;
	feed(s, 100);
	zassert_true(cp.diode_fault);
	evse_cp_clear_diode(&cp);
	zassert_false(cp.diode_fault);
}

ZTEST(evse_cp, test_diode_not_checked_when_blocked)
{
	struct evse_cp_sample s = {
		.pos_mv = 9000, .neg_mv = 5000, .pos_valid = true, .neg_valid = true,
		.pwm_active = true, .pwm_since = 0, .current_blocked = true,
	};

	feed(s, 400);
	zassert_false(cp.diode_fault);
	s.current_blocked = false;
	s.pwm_active = false;
	feed(s, 400);
	zassert_false(cp.diode_fault);
}

ZTEST(evse_cp, test_short_during_pwm_is_invalid)
{
	/* SOLL-CP-06/07: Kurzschluss bei PWM senkt beide Plateaus -> ungueltig, keine Diode */
	struct evse_cp_sample s = {
		.pos_mv = 6000, .neg_mv = 12000, .pos_valid = true, .neg_valid = true,
		.pwm_active = true, .pwm_since = 0, .current_blocked = false,
	};

	feed(s, 200);
	zassert_equal(evse_cp_raw(&cp), EVSE_CP_C);
	s.pos_mv = 300;
	s.neg_mv = 300;
	feed(s, 200);
	zassert_true(cp.invalid);
	zassert_false(cp.diode_fault, "Kurzschluss ist keine fehlende Diode");
	/* nach dem Loslassen wieder C, kein Fehler */
	s.pos_mv = 6000;
	s.neg_mv = 12000;
	feed(s, 100);
	zassert_false(cp.invalid);
	zassert_false(cp.diode_fault);
	/* fehlende Diode bei Zustand D wird weiter erkannt */
	s.pos_mv = 3000;
	s.neg_mv = 3000;
	feed(s, 100);
	zassert_true(cp.diode_fault);
}

ZTEST_SUITE(evse_cp, NULL, NULL, before, NULL, NULL);
