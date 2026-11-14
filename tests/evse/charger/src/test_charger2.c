/* SPDX-License-Identifier: Apache-2.0 */
/* Weitere Szenarien: manuelle Verriegelung, Fehlerpfade, Eingangsfunktionen */
#include <zephyr/ztest.h>
#include <wallfree/api/evse_api.h>
#include "sim.h"

#define DIP(n) ((evse_dip_t)(1u << ((n) - 1)))

static struct sim s;
static struct evse_config cfg;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	sim_init(&s, &cfg);
}

static void reinit(void)
{
	sim_init(&s, &cfg);
}

static void charge(void)
{
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 15000) >= 0, "Zustand %d", s.e.state);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0, "Zustand %d", s.e.state);
}

ZTEST(evse_charger2, test_manual_lock_d9)
{
	/* D9: Verriegelung folgt ML (Code 4), auch ohne Fahrzeug (SOLL-LOCK-24) */
	cfg.dip = DIP(4) | DIP(6) | DIP(9);
	cfg.din.code[EVSE_DIN_LD] = EVSE_IN_LD_PULSED;
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S1_IDLE, "ohne ML kein Verriegeln, kein Pendeln");
	zassert_false(s.actuator_locked);
	s.din_mv[EVSE_DIN_ML] = 12000;
	charge();
	zassert_true(s.actuator_locked);
	/* ML aus waehrend des Ladens: geordnetes Ende, dann entriegeln (E7c) */
	s.din_mv[EVSE_DIN_ML] = 0;
	sim_run(&s, 30);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 6000) >= 0, "Zustand %d", s.e.state);
	zassert_false(s.actuator_locked);
	/* Verriegeln ohne Fahrzeug */
	s.car = EVSE_CP_A;
	s.din_mv[EVSE_DIN_ML] = 12000;
	sim_run(&s, 4000);
	zassert_true(s.actuator_locked);
	zassert_equal(s.e.state, EVSE_S2_LOCKING);
}

ZTEST(evse_charger2, test_manual_lock_toggle)
{
	cfg.dip = DIP(4) | DIP(9);
	cfg.din.code[EVSE_DIN_ML] = EVSE_IN_ML_PULSED;
	reinit();
	sim_boot(&s);
	s.din_mv[EVSE_DIN_ML] = 12000;
	sim_run(&s, 100);
	s.din_mv[EVSE_DIN_ML] = 0;
	sim_run(&s, 3000);
	zassert_true(s.e.rt.lock_request);
	zassert_true(s.actuator_locked);
	s.din_mv[EVSE_DIN_ML] = 12000;
	sim_run(&s, 100);
	s.din_mv[EVSE_DIN_ML] = 0;
	sim_run(&s, 4000);
	zassert_false(s.e.rt.lock_request);
	zassert_false(s.actuator_locked);
}

ZTEST(evse_charger2, test_unlock_stuck)
{
	/* SOLL-LOCK-22: 4 Versuche, dann Fehler; kein Dauerversuch nach Abstecken */
	cfg.dip = DIP(4) | DIP(6);
	reinit();
	s.ld_model = LD_MODEL_PERMANENT;
	sim_boot(&s);
	charge();
	s.car = EVSE_CP_B;
	sim_run(&s, 50);
	s.actuator_stuck = true;
	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S9_ERROR, 30000) >= 0, "Zustand %d", s.e.state);
	zassert_true(s.e.errors & EVSE_ERR_UNLOCK);
	zassert_equal(s.e.unlock_attempts, EVSE_UNLOCK_ATTEMPTS);
	zassert_true(s.actuator_locked);
	zassert_equal(s.e.lock.state, EVSE_LOCK_LOCKED);
	/* danach keine weiteren Impulse */
	int pulses = 0;
	bool was = false;

	for (int i = 0; i < 20000; i++) {
		sim_run(&s, 1);
		bool p = s.e.out.unlock_pulse || s.e.out.lock_pulse;

		pulses += p && !was;
		was = p;
	}
	zassert_equal(pulses, 0, "%d Impulse", pulses);
	zassert_true(s.e.errors & EVSE_ERR_UNLOCK);
}

ZTEST(evse_charger2, test_relock_fails)
{
	/* SOLL-LOCK-40: Wiederverriegeln misslingt -> S9 bis CP A */
	cfg.dip = DIP(4) | DIP(6);
	reinit();
	s.ld_model = LD_MODEL_PERMANENT;
	sim_boot(&s);
	charge();
	s.ld_force_off = true;
	zassert_true(sim_until_state(&s, EVSE_S9_ERROR, 6000) >= 0, "Zustand %d", s.e.state);
	zassert_true(s.e.errors & EVSE_ERR_LD_LOST);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.contactor_on_count, 1);
}

ZTEST(evse_charger2, test_init_unlock_failed)
{
	/* SOLL-LOCK-20: Start-Entriegelung misslingt */
	cfg.dip = DIP(4) | DIP(6);
	reinit();
	s.ld_model = LD_MODEL_PERMANENT;
	s.actuator_locked = true;
	s.actuator_stuck = true;
	sim_start(&s);
	sim_run(&s, 6000);
	zassert_equal(s.e.unlock_attempts, 1, "Startversuch zaehlt");
	/* weitere Versuche im Ablauf, insgesamt 4, dann Fehler */
	zassert_true(sim_until_state(&s, EVSE_S9_ERROR, 30000) >= 0, "Zustand %d", s.e.state);
	zassert_true(s.e.errors & EVSE_ERR_UNLOCK);
	zassert_equal(s.e.unlock_attempts, EVSE_UNLOCK_ATTEMPTS);
}

ZTEST(evse_charger2, test_overcurrent_with_meter)
{
	/* SOLL-PROT-31, E22: 1,25 x fuer 5 s */
	cfg.meter_available = true;
	cfg.overcurrent_enable = true;
	reinit();
	s.meter_valid = true;
	sim_boot(&s);
	charge();
	for (int i = 0; i < 3; i++) {
		s.phase_ma[i] = 20000; /* 1,25 x 15960 mA = 19950 */
	}
	sim_run(&s, 4900);
	zassert_true(s.e.out.contactor);
	sim_run(&s, 200);
	zassert_false(s.e.out.contactor);
	zassert_true(s.e.errors & EVSE_ERR_OVERCURRENT);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	s.car = EVSE_CP_A;
	sim_until_state(&s, EVSE_S1_IDLE, 200);
	zassert_false(s.e.errors & EVSE_ERR_OVERCURRENT);
}

ZTEST(evse_charger2, test_overcurrent_110_95s)
{
	cfg.meter_available = true;
	cfg.overcurrent_enable = true;
	reinit();
	s.meter_valid = true;
	sim_boot(&s);
	charge();
	for (int i = 0; i < 3; i++) {
		s.phase_ma[i] = 17600; /* > 1,1 x, < 1,25 x */
	}
	sim_run(&s, 94900);
	zassert_true(s.e.out.contactor);
	sim_run(&s, 200);
	zassert_false(s.e.out.contactor);
}

ZTEST(evse_charger2, test_meter_lost)
{
	cfg.meter_available = true;
	cfg.overcurrent_enable = true;
	reinit();
	s.meter_valid = true;
	sim_boot(&s);
	charge();
	s.meter_valid = false;
	sim_run(&s, 20);
	zassert_true(s.e.errors & EVSE_ERR_METER_COMM);
	zassert_equal(s.e.state, EVSE_S7_STOPPING, "geordnetes Ende");
}

ZTEST(evse_charger2, test_meter_mode_without_meter)
{
	cfg.contactor_monitor = EVSE_CTM_METER;
	reinit();
	sim_start(&s);
	zassert_true(s.e.errors & EVSE_ERR_CONFIG);
}

ZTEST(evse_charger2, test_supply_warning_only)
{
	/* F16 */
	sim_boot(&s);
	charge();
	s.supply_mv = 8000;
	sim_run(&s, 200);
	zassert_true(s.e.warnings & EVSE_WARN_SUPPLY);
	zassert_true(s.e.out.contactor, "keine Abschaltung");
	s.supply_mv = 9500;
	sim_run(&s, 3000);
	zassert_true(s.e.warnings & EVSE_WARN_SUPPLY, "Hysterese");
	s.supply_mv = 12000;
	sim_run(&s, 2600);
	zassert_false(s.e.warnings & EVSE_WARN_SUPPLY);
}

ZTEST(evse_charger2, test_cable_reject)
{
	/* D1+D2 (D3 aus): 20-A-Kabel abweisen */
	cfg.dip = DIP(1) | DIP(2);
	reinit();
	sim_boot(&s);
	s.pp_mv = 4000; /* 20 A */
	s.car = EVSE_CP_B;
	sim_run(&s, 3000);
	zassert_true(s.e.errors & EVSE_ERR_CABLE_REJECT);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	zassert_equal(s.contactor_on_count, 0);
	zassert_true(evse_dout_eval(&s.e, 26, s.now));
}

ZTEST(evse_charger2, test_cable_limit_and_pp_lost)
{
	/* D1: 13-A-Kabel begrenzt; PP faellt im Laden weg -> sofort aus */
	cfg.dip = DIP(1);
	reinit();
	sim_boot(&s);
	s.pp_mv = 6000; /* 13 A */
	sim_run(&s, 1100);
	charge();
	zassert_equal(s.e.out.cp_duty, 216);
	s.pp_mv = 9000; /* kein Kabel bei Fahrzeug */
	sim_run(&s, 1100);
	zassert_false(s.e.out.contactor);
	zassert_true(s.e.errors & EVSE_ERR_PP_INVALID);
}

ZTEST(evse_charger2, test_error_input)
{
	cfg.din.code[EVSE_DIN_IN] = EVSE_IN_ERROR;
	reinit();
	sim_boot(&s);
	charge();
	s.din_mv[EVSE_DIN_IN] = 12000;
	sim_run(&s, 30);
	zassert_false(s.e.out.contactor, "sofort (F8)");
	zassert_true(s.e.errors & EVSE_ERR_INPUT);
	s.din_mv[EVSE_DIN_IN] = 0;
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 3000) >= 0, "Zustand %d", s.e.state);
}

ZTEST(evse_charger2, test_input_pwm5_pause_maxcurrent)
{
	cfg.din.code[EVSE_DIN_IN] = EVSE_IN_PWM5;
	cfg.din.code[EVSE_DIN_XR] = EVSE_IN_PAUSE;
	cfg.din.code[EVSE_DIN_ML] = EVSE_IN_MAX_CURRENT;
	reinit();
	sim_boot(&s);
	s.din_mv[EVSE_DIN_IN] = 12000;
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 4000);
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 50);
	s.din_mv[EVSE_DIN_XR] = 12000; /* Pause hat Vorrang */
	sim_run(&s, 30);
	zassert_equal(s.e.state, EVSE_S3_WAIT);
	s.din_mv[EVSE_DIN_XR] = 0;
	s.din_mv[EVSE_DIN_IN] = 0;
	s.e.rt.external_ma = 0; /* evcc sperrt ... */
	sim_run(&s, 30);
	zassert_equal(s.e.state, EVSE_S3_WAIT);
	s.din_mv[EVSE_DIN_ML] = 12000; /* ... Code 19 ueberstimmt (F10) */
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S5_PWM_ON);
	zassert_equal(s.e.out.cp_duty, 266);
	zassert_true(evse_dout_eval(&s.e, 38, s.now));
}

ZTEST(evse_charger2, test_pwm5_request_and_limit_override)
{
	sim_boot(&s);
	s.e.rt.hardware_limit_ma = 10000; /* SOLL-PAR-40 absenken */
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 4000);
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 166);
	s.e.rt.hardware_limit_ma = 32000; /* hoeher als Drehschalter: wirkungslos */
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 266);
	s.e.rt.pwm5_request = true;
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 50);
}

ZTEST(evse_charger2, test_available_register_d10)
{
	cfg.dip = DIP(10);
	reinit();
	sim_boot(&s);
	s.e.rt.external_ma = 16000; /* F37: Freigabe mit D10 nur ueber external */
	s.e.rt.available_register = false;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	s.e.rt.available_register = true;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
}

ZTEST(evse_charger2, test_wakeup_aborted_by_unplug)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 4000);
	sim_run(&s, 31000);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	/* waehrend -12 V kein Plateau: Abstecken wird in Phase 2 erkannt */
	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 4000) >= 0, "Zustand %d", s.e.state);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
}

ZTEST(evse_charger2, test_rcm_during_selftest_state_unplug)
{
	/* SOLL-SM-42: Abbruch des Selbsttests durch Abstecken */
	sim_boot(&s);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S4_SELFTEST, 1000) >= 0);
	sim_run(&s, 500);
	s.car = EVSE_CP_A;
	sim_run(&s, 100);
	zassert_equal(s.e.state, EVSE_S1_IDLE, "Zustand %d Fehler 0x%x raw %d", s.e.state, s.e.errors,
		      evse_cp_raw(&s.e.cp));
	zassert_equal(s.e.errors, 0);
	zassert_false(s.e.out.rcm_test);
}

ZTEST(evse_charger2, test_charge_time)
{
	sim_boot(&s);
	charge();
	sim_run(&s, 10000);
	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	zassert_true(s.e.charge_ms >= 9900 && s.e.charge_ms <= 10200, "%lld",
		     (long long)s.e.charge_ms);
}

ZTEST(evse_charger2, test_protect_trip_while_charging)
{
	/* SOLL-PROT-14: Schutzmerker gesetzt, Automat kennt keinen Fehlerstrom -> ERR-RCM-TRIP
	 * statt S6 mit abgeschaltetem Schuetz (Testgeraet 2026-11-13) */
	sim_boot(&s);
	charge();
	s.protect_tripped = true;
	sim_run(&s, 10);
	zassert_true(s.e.errors & EVSE_ERR_RCM_TRIP, "Fehler 0x%x", s.e.errors);
	zassert_false(s.e.out.contactor);
	zassert_true(sim_until_state(&s, EVSE_S9_ERROR, 6000) >= 0, "Zustand %d", s.e.state);
	/* Merker loest die Hardwareschicht erst, wenn das Schuetz aus sein soll */
	s.protect_tripped = false;
	sim_run(&s, 1000);
	zassert_true(s.e.errors & EVSE_ERR_RCM_TRIP, "verriegelnd bis zum Selbsttest in A");
	/* Rücksetzung wie bei Fehlerstrom: Abstecken, Selbsttest in A (SOLL-PROT-08/11) */
	s.car = EVSE_CP_A;
	sim_run(&s, 40000);
	zassert_false(s.e.errors & EVSE_ERR_RCM_TRIP, "Fehler 0x%x", s.e.errors);
	zassert_equal(s.e.state, EVSE_S1_IDLE, "Zustand %d", s.e.state);
}

ZTEST(evse_charger2, test_protect_trip_contactor_off_no_error)
{
	/* SOLL-PROT-14: bei Schuetz aus (z. B. Testimpuls) kein Fehler */
	sim_boot(&s);
	s.protect_tripped = true;
	sim_run(&s, 100);
	zassert_equal(s.e.errors, 0, "Fehler 0x%x", s.e.errors);
	s.protect_tripped = false;
	charge();
	zassert_equal(s.e.errors, 0);
}

ZTEST(evse_charger2, test_contactor_no_pull)
{
	/* SOLL-PROT-25: Rueckmeldung fehlt nach dem Einschalten -> ERR-CONTACTOR nach 2 s */
	sim_boot(&s);
	s.no_pull = true;
	charge();
	sim_run(&s, 1900);
	zassert_false(s.e.errors & EVSE_ERR_CONTACTOR, "frühestens nach 2 s");
	sim_run(&s, 200);
	zassert_true(s.e.errors & EVSE_ERR_CONTACTOR, "Fehler 0x%x", s.e.errors);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE, "Zustand %d", s.e.state);
	zassert_equal(api_contactor_error(&s.e), 2, "WARP: sollte geschlossen sein");
}

ZTEST(evse_charger2, test_c1_reported_as_b)
{
	/* F36: Fahrzeug bleibt nach dem Ausschalten in C (Nissan Leaf, C1) -> WARP-Zustand B */
	sim_boot(&s);
	charge();
	zassert_equal(api_iec61851_state(&s.e), 2, "lädt: C");
	s.e.rt.external_ma = 0; /* evcc: Laden aus */
	sim_run(&s, 100);
	zassert_equal(evse_allowed_current(&s.e), 0);
	zassert_equal(evse_cp_raw(&s.e.cp), EVSE_CP_C, "Fahrzeug bleibt in C");
	zassert_equal(api_iec61851_state(&s.e), 1, "kein Strom erlaubt: B");
	sim_run(&s, 10000);
	zassert_false(s.e.out.contactor);
	zassert_equal(api_iec61851_state(&s.e), 1);
	/* wieder freigegeben: PWM an, Fahrzeug in C -> C */
	s.e.rt.external_ma = 16000;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 10000) >= 0, "Zustand %d", s.e.state);
	zassert_equal(api_iec61851_state(&s.e), 2);
}

ZTEST(evse_charger2, test_contactor_pulls_no_error)
{
	sim_boot(&s);
	charge();
	sim_run(&s, 5000);
	zassert_equal(s.e.errors, 0, "Fehler 0x%x", s.e.errors);
	zassert_equal(s.e.state, EVSE_S6_CHARGING);
}

ZTEST_SUITE(evse_charger2, NULL, NULL, before, NULL, NULL);
