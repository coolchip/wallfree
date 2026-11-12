/* SPDX-License-Identifier: Apache-2.0 */
/* Szenariotests Ladeautomat (Abschnitte 5, 7, 8, 9, 11) */
#include <zephyr/ztest.h>
#include "sim.h"

#define DIP(n) ((evse_dip_t)(1u << ((n) - 1)))

static struct sim s;
static struct evse_config cfg;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	cfg.contactor_monitor = EVSE_CTM_SENSE;
	sim_init(&s, &cfg);
}

static void reinit(void)
{
	sim_init(&s, &cfg);
}

/* Fahrzeug anstecken und bis PWM an */
static void plug_to_pwm(void)
{
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 10000) >= 0, "Zustand %d", s.e.state);
}

ZTEST(evse_charger, test_startup_5s)
{
	/* SOLL-SM-01/02 */
	sim_start(&s);
	sim_run(&s, 4900);
	zassert_equal(s.e.state, EVSE_S0_INIT);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	zassert_false(s.e.out.contactor);
	sim_run(&s, 200);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
	zassert_equal(s.e.rcm.test, EVSE_RCM_TEST_PASSED);
	zassert_equal(s.e.errors, 0, "Fehler 0x%x", s.e.errors);
}

ZTEST(evse_charger, test_basic_charge_cycle)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	zassert_true(s.e.state == EVSE_S4_SELFTEST, "Zustand %d", s.e.state);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF, "kein PWM vor dem Selbsttest");
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 266, "16 A Drehschalter");
	zassert_false(s.e.out.contactor);

	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	zassert_true(s.e.out.contactor);
	/* Defaults: ER 15 aus, LR 29 aus (ohne D4 Wunsch "verriegeln" gesetzt), VR 16, CR 17 */
	zassert_false(s.e.out.dout[EVSE_DOUT_ER]);
	zassert_true(s.e.out.dout[EVSE_DOUT_VR]);
	zassert_true(s.e.out.dout[EVSE_DOUT_CR]);
	zassert_equal(s.e.out.led_mode[EVSE_LED_READY], EVSE_LEDM_ON);

	/* SOLL-SM-62: Fahrzeug beendet -> sofort aus, PWM bleibt */
	s.car = EVSE_CP_B;
	sim_run(&s, 40);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.e.state, EVSE_S5_PWM_ON);
	zassert_equal(s.e.out.cp_duty, 266);
	zassert_equal(s.e.rcm.test, EVSE_RCM_TEST_PASSED, "kein neuer Selbsttest (E18)");

	zassert_equal(s.e.lock.state, EVSE_LOCK_LOCKED, "ohne D4 sofort verriegelt (SOLL-LOCK-01)");

	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 200) >= 0);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	/* SOLL-LOCK-01/23: ohne D4 bei CP A sofort entriegelt (blieb vorher LOCKED) */
	sim_run(&s, 100);
	zassert_equal(s.e.lock.state, EVSE_LOCK_UNLOCKED);
	zassert_equal(s.e.out.led_mode[EVSE_LED_CONNECT], EVSE_LEDM_OFF, "SOLL-LED-03");
}

ZTEST(evse_charger, test_selftest_once_per_plug)
{
	/* E18: Ruecksprung ueber Stromsperre ohne neuen Selbsttest */
	sim_boot(&s);
	plug_to_pwm();
	s.e.rt.external_ma = 0;
	zassert_true(sim_until_state(&s, EVSE_S3_WAIT, 100) >= 0);
	s.e.rt.external_ma = 10000;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S5_PWM_ON);
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 166);
}

ZTEST(evse_charger, test_ordered_stop_5s)
{
	/* E7b, SOLL-SM-70/71: Stromsperre im Laden, Fahrzeug reagiert nicht */
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	s.e.rt.external_ma = 0;
	sim_run(&s, 20);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	zassert_true(s.e.out.contactor);
	sim_run(&s, 4950);
	zassert_true(s.e.out.contactor);
	sim_run(&s, 100);
	zassert_false(s.e.out.contactor, "spaetestens nach 5 s aus");
	zassert_equal(s.e.state, EVSE_S3_WAIT);
}

ZTEST(evse_charger, test_ordered_stop_vehicle_reacts)
{
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.e.rt.pause_request = true;
	sim_run(&s, 1000);
	zassert_true(s.e.out.contactor);
	s.car = EVSE_CP_B;
	sim_run(&s, 40);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.e.state, EVSE_S3_WAIT);
}

ZTEST(evse_charger, test_rcm_trip_immediate_and_latched)
{
	/* E1, SOLL-PROT-04/08/11 */
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.rcm_fault_current = true;
	sim_run(&s, 1);
	zassert_false(s.e.out.contactor, "im selben Zyklus aus");
	zassert_true(s.e.errors & EVSE_ERR_RCM_DC6);
	s.rcm_fault_current = false;
	sim_run(&s, 1000);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	zassert_true(s.e.errors & EVSE_ERR_RCM_ANY, "verriegelnd");
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	/* Abstecken -> Selbsttest -> Ruecksetzen */
	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 5000) >= 0);
	zassert_equal(s.e.errors, 0);
}

ZTEST(evse_charger, test_rcm_retest_interval)
{
	/* F13: fehlschlagender Test frueheste Wiederholung nach 30 s */
	sim_boot(&s);
	s.rcm_fault_current = true;
	sim_run(&s, 10);
	s.rcm_fault_current = false;
	s.rcm_healthy = false; /* Sensor reagiert nicht mehr auf den Test */
	sim_run(&s, 5000);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	evse_ms_t first = s.e.rcm_retest_last;

	zassert_true(first != EVSE_MS_NONE);
	sim_run(&s, 20000);
	zassert_equal(s.e.rcm_retest_last, first);
	s.rcm_healthy = true;
	sim_run(&s, 15000);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
}

ZTEST(evse_charger, test_wakeup_once)
{
	/* E8/E17: nach 30 s B einmalig 3 s -12 V, 0,75 s +12 V */
	sim_boot(&s);
	plug_to_pwm();
	sim_run(&s, 29900);
	zassert_equal(s.e.out.cp_duty, 266);
	sim_run(&s, 200);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	sim_run(&s, 2900);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	sim_run(&s, 200);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	sim_run(&s, 800);
	zassert_equal(s.e.out.cp_duty, 266);
	zassert_equal(s.e.state, EVSE_S5_PWM_ON);
	zassert_equal(s.e.errors, 0, "Weckfolge erzeugt keinen CP-Fehler");
	sim_run(&s, 40000);
	zassert_equal(s.e.out.cp_duty, 266, "nur einmal je Steckvorgang");
}

ZTEST(evse_charger, test_wakeup_wallbe_mode)
{
	cfg.wakeup_delay_ms = 5000;
	cfg.wakeup_mode = 1;
	reinit();
	sim_boot(&s);
	plug_to_pwm();
	sim_run(&s, 5100);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	sim_run(&s, 4000);
	/* Stromsperre und Rueckkehr: Weckfolge erneut zulaessig */
	s.e.rt.external_ma = 0;
	sim_run(&s, 100);
	s.e.rt.external_ma = 16000;
	sim_run(&s, 5200);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
}

ZTEST(evse_charger, test_state_d)
{
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_D;
	sim_run(&s, 100);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	zassert_true(s.e.errors & EVSE_ERR_STATE_D);
	zassert_equal(s.contactor_on_count, 0);
	s.car = EVSE_CP_A;
	sim_until_state(&s, EVSE_S1_IDLE, 200);
	zassert_equal(s.e.errors, 0);

	cfg.reject_state_d = false;
	reinit();
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_D;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	zassert_true(evse_dout_eval(&s.e, 28, s.now), "Belueftung");
}

ZTEST(evse_charger, test_pwm5_never_closes)
{
	/* E6, SOLL-CUR-12, SOLL-SM-53 */
	cfg.rotary = 0;
	reinit();
	sim_boot(&s);
	plug_to_pwm();
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 50);
	s.car = EVSE_CP_C;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S5_PWM_ON);
	zassert_false(s.e.out.contactor);
	zassert_true(s.e.digital_comm_unavailable);
	zassert_equal(s.contactor_on_count, 0);
}

ZTEST(evse_charger, test_rotary_invalid)
{
	/* E11 */
	cfg.rotary = 12;
	reinit();
	sim_start(&s);
	sim_run(&s, 6000);
	zassert_true(s.e.errors & EVSE_ERR_SWITCH);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	s.car = EVSE_CP_C;
	sim_run(&s, 5000);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	zassert_equal(s.contactor_on_count, 0);
}

ZTEST(evse_charger, test_lock_kuester_pulsed)
{
	/* D4+D6, LD gepulst (Code 22 auf LD), Kuester-Modell */
	cfg.dip = DIP(4) | DIP(6);
	cfg.din.code[EVSE_DIN_LD] = EVSE_IN_LD_PULSED;
	reinit();
	s.ld_model = LD_MODEL_KUESTER;
	sim_boot(&s);
	zassert_equal(s.e.lock.state, EVSE_LOCK_UNLOCKED);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S3_WAIT, 6000) >= 0, "Zustand %d", s.e.state);
	zassert_true(s.actuator_locked);
	zassert_equal(s.e.lock.state, EVSE_LOCK_LOCKED);
	zassert_true(s.e.out.led_mode[EVSE_LED_CONNECT] == EVSE_LEDM_ON);
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 6000) >= 0);
	zassert_false(s.actuator_locked);
	zassert_equal(s.e.lock.state, EVSE_LOCK_UNLOCKED);
}

ZTEST(evse_charger, test_lock_permanent_fails_with_kuester)
{
	/* Code 3 an einem Kuester-Aktor: Verriegelung gilt nie als erfolgreich */
	cfg.dip = DIP(4) | DIP(6);
	reinit();
	s.ld_model = LD_MODEL_KUESTER;
	sim_boot(&s);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S9_ERROR, 60000) >= 0, "Zustand %d", s.e.state);
	zassert_true(s.e.errors & EVSE_ERR_LOCK);
	zassert_equal(s.e.lock_attempts, EVSE_LOCK_ATTEMPTS);
	zassert_equal(s.contactor_on_count, 0);
	s.car = EVSE_CP_A;
	sim_run(&s, 100);
	zassert_false(s.e.errors & EVSE_ERR_LOCK);
}

ZTEST(evse_charger, test_ld_lost_relock)
{
	/* SOLL-LOCK-40 (F9) mit permanentem Kontakt */
	cfg.dip = DIP(4) | DIP(6);
	reinit();
	s.ld_model = LD_MODEL_PERMANENT;
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	s.ld_force_off = true;
	sim_run(&s, 140);
	zassert_false(s.e.out.contactor, "sofort aus");
	zassert_true(s.e.errors & EVSE_ERR_LD_LOST);
	zassert_equal(s.e.state, EVSE_S2_LOCKING);
	s.ld_force_off = false;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 6000) >= 0, "Zustand %d", s.e.state);
	zassert_false(s.e.errors & EVSE_ERR_LD_LOST);
	zassert_equal(s.contactor_on_count, 2);
}

ZTEST(evse_charger, test_availability_xr)
{
	/* D8: XR fehlt -> F (-12 V); waehrend des Ladens geordnetes Ende */
	cfg.dip = DIP(8);
	reinit();
	s.din_mv[EVSE_DIN_XR] = 12000;
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.din_mv[EVSE_DIN_XR] = 0;
	sim_run(&s, 30);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
	s.car = EVSE_CP_B;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	zassert_equal(s.e.status, EVSE_CP_F);
	s.din_mv[EVSE_DIN_XR] = 12000;
	sim_run(&s, 100);
	zassert_not_equal(s.e.state, EVSE_S10_UNAVAILABLE);
}

ZTEST(evse_charger, test_availability_lost_vehicle_ignores)
{
	/* Regression: S7 mit Ziel S10 muss die 5-s-Frist abarbeiten */
	cfg.dip = DIP(8);
	reinit();
	s.din_mv[EVSE_DIN_XR] = 12000;
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.din_mv[EVSE_DIN_XR] = 0;
	sim_run(&s, 4900);
	zassert_true(s.e.out.contactor);
	sim_run(&s, 200);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE);
}

ZTEST(evse_charger, test_diode_missing)
{
	/* SOLL-CP-07, F7 */
	sim_boot(&s);
	s.diode = false;
	plug_to_pwm();
	sim_run(&s, 200);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
	zassert_true(s.e.errors & EVSE_ERR_DIODE);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	sim_run(&s, 3000);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF, "nach 3 s +12 V");
	s.car = EVSE_CP_A;
	zassert_true(sim_until_state(&s, EVSE_S1_IDLE, 200) >= 0);
	zassert_false(s.e.errors & EVSE_ERR_DIODE);
}

ZTEST(evse_charger, test_contactor_welded)
{
	/* SOLL-PROT-21/23, F14 */
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.welded = true;
	s.car = EVSE_CP_B;
	sim_run(&s, 1000);
	zassert_true(s.e.errors & EVSE_ERR_CONTACTOR);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	s.welded = false;
	s.car = EVSE_CP_A;
	sim_run(&s, 1000);
	zassert_true(s.e.errors & EVSE_ERR_CONTACTOR, "kein automatisches Loeschen");
	zassert_true(evse_ack_contactor_fault(&s.e));
	sim_run(&s, 100);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
}

ZTEST(evse_charger, test_permission_en)
{
	/* D7: Freigabe ueber EN (Schluesselschalter) */
	cfg.dip = DIP(7);
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 5000);
	zassert_equal(s.e.state, EVSE_S3_WAIT);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_OFF);
	s.din_mv[EVSE_DIN_EN] = 12000;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.din_mv[EVSE_DIN_EN] = 0;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
}

ZTEST(evse_charger, test_permission_register_d10)
{
	cfg.dip = DIP(10);
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S3_WAIT, "400 startet mit 0 (F19)");
	s.e.rt.enable_register = true;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S3_WAIT, "external startet mit D10 bei 0 (F34)");
	s.e.rt.external_ma = 16000;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
}

ZTEST(evse_charger, test_external_start_value_d10)
{
	/* SOLL-CUR-15, F34 */
	cfg.dip = DIP(10);
	reinit();
	sim_start(&s);
	zassert_equal(s.e.rt.external_ma, 0);
	cfg.dip = 0;
	reinit();
	sim_start(&s);
	zassert_equal(s.e.rt.external_ma, 80000);
}

ZTEST(evse_charger, test_external_releases_d10)
{
	/* SOLL-EN-01 b, SOLL-EN-12: evcc startet und beendet mit D10 allein ueber den Strom */
	cfg.dip = DIP(10);
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S3_WAIT);
	zassert_false(s.e.permission);
	s.e.rt.external_ma = 10000;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
	zassert_true(s.e.permission);
	zassert_equal(s.e.out.cp_duty, 166);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 2000) >= 0);
	s.e.rt.external_ma = 0;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
	zassert_false(s.e.permission);
}

ZTEST(evse_charger, test_key_forces_charging_d7_d10)
{
	/* SOLL-EN-13: Schluessel hebt die evcc-Vorgabe auf, auch bei 0 */
	cfg.dip = DIP(7) | DIP(10);
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S3_WAIT, "ohne evcc und ohne Schluessel kein Laden");
	s.din_mv[EVSE_DIN_EN] = 12000;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 4000) >= 0);
	zassert_true(s.e.cur_in.external_ignored);
	zassert_equal(s.e.out.cp_duty, 266, "Drehschalter 16 A trotz external 0");
	s.e.rt.external_ma = 6000;
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 266, "evcc-Wert wirkungslos");
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 2000) >= 0);
	/* Schluessel aus: evcc-Wert gilt sofort wieder */
	s.din_mv[EVSE_DIN_EN] = 0;
	sim_run(&s, 50);
	zassert_false(s.e.cur_in.external_ignored);
	zassert_equal(s.e.state, EVSE_S6_CHARGING);
	zassert_equal(s.e.out.cp_duty, 100);
	/* evcc 0: geordnetes Ende */
	s.e.rt.external_ma = 0;
	sim_run(&s, 50);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
}

ZTEST(evse_charger, test_key_d7_only_keeps_external)
{
	/* SOLL-EN-13: mit D7 allein ist EN nur Erlaubnis */
	cfg.dip = DIP(7);
	reinit();
	sim_boot(&s);
	s.e.rt.external_ma = 6000;
	s.din_mv[EVSE_DIN_EN] = 12000;
	plug_to_pwm();
	zassert_false(s.e.cur_in.external_ignored);
	zassert_equal(s.e.out.cp_duty, 100);
}

ZTEST(evse_charger, test_unlock_request_during_charge)
{
	/* E7c, SOLL-LOCK-25 */
	cfg.dip = DIP(4) | DIP(6);
	cfg.din.code[EVSE_DIN_LD] = EVSE_IN_LD_PULSED;
	reinit();
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 500) >= 0);
	s.e.rt.unlock_request = true;
	sim_run(&s, 20);
	zassert_equal(s.e.state, EVSE_S7_STOPPING);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S8_UNLOCKING, 200) >= 0);
	zassert_false(s.e.out.contactor);
	sim_run(&s, 4000);
	zassert_false(s.actuator_locked);
	sim_run(&s, 10000);
	zassert_false(s.actuator_locked, "kein erneutes Verriegeln bis CP A");
	zassert_equal(s.contactor_on_count, 1);
}

ZTEST(evse_charger, test_cp_short)
{
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	s.car = EVSE_CP_E; /* 0 V */
	sim_run(&s, 40);
	zassert_false(s.e.out.contactor);
	zassert_true(s.e.errors & EVSE_ERR_CP_INVALID);
	zassert_equal(s.e.state, EVSE_S9_ERROR);
}

ZTEST(evse_charger, test_external_current_slot)
{
	sim_boot(&s);
	plug_to_pwm();
	s.e.rt.external_ma = 32000; /* F11: ueber Drehschalter -> 16 A */
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 266);
	s.e.rt.external_ma = 6000;
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 100);
	s.din_mv[EVSE_DIN_IN] = 12000; /* Code 11: 16 A, groesser als 6 A */
	sim_run(&s, 30);
	zassert_equal(s.e.out.cp_duty, 100);
}

/* F12: Werkswert ohne Einschraenkung; der Drehschalter (Default Stellung 4 = 16 A) begrenzt */
ZTEST(evse_charger, test_external_default_unlimited)
{
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	zassert_equal(cfg.external_current_default_ma, EVSE_CURRENT_MAX_MA);
	reinit();
	sim_boot(&s);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 10000) >= 0, "Zustand %d", s.e.state);
	zassert_equal(s.e.out.cp_duty, 266, "16 A Drehschalter");
	zassert_equal(s.e.cur_in.external_ma, EVSE_CURRENT_MAX_MA);
	zassert_equal(s.e.cur_in.hardware_ma, 16000);
}

ZTEST(evse_charger, test_safe_state)
{
	sim_boot(&s);
	plug_to_pwm();
	s.car = EVSE_CP_C;
	sim_until_state(&s, EVSE_S6_CHARGING, 500);
	evse_safe_state(&s.e, s.now);
	zassert_false(s.e.out.contactor);
	zassert_equal(s.e.out.cp_duty, EVSE_DUTY_F);
	sim_run(&s, 100);
	zassert_false(s.e.out.contactor, "bleibt sicher");
}

ZTEST_SUITE(evse_charger, NULL, NULL, before, NULL, NULL);
