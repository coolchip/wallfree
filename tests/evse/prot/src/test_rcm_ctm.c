/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-PROT-04..13 (RCM) und SOLL-PROT-20..23 (Schuetzueberwachung) */
#include <zephyr/ztest.h>
#include <wallfree/evse/rcm.h>
#include <wallfree/evse/ctm.h>

static struct evse_rcm rcm;
static struct evse_rcm_levels lv;
static evse_ms_t now;

/* Bender-Verhalten im Test nachbilden: SWITCH_2 ab t_sw2, SWITCH_1 ab t_sw1,
 * beide bis t_rel ausgeloest (Zeiten ab Teststart). */
struct sensor_model {
	int t_sw2, t_sw1, t_rel;
};

static void run_test(struct sensor_model m)
{
	evse_ms_t start = now;

	while (rcm.test == EVSE_RCM_TEST_RUNNING && now - start < 5000) {
		now += 5;
		int t = (int)(now - start);
		bool active = rcm.out_test || t >= 300; /* Testwicklung ab Impuls */

		lv.sw2 = active && m.t_sw2 >= 0 && t >= m.t_sw2 && t < m.t_rel;
		lv.sw1 = active && m.t_sw1 >= 0 && t >= m.t_sw1 && t < m.t_rel;
		evse_rcm_selftest_update(&rcm, &lv, now);
	}
}

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_rcm_init(&rcm);
	lv = (struct evse_rcm_levels){0};
	now = 0;
}

ZTEST(evse_rcm, test_check_all_bits)
{
	struct evse_rcm_levels l = {.sw1 = true, .sw2 = true, .err = true};

	zassert_equal(evse_rcm_check(&l),
		      EVSE_RCM_F_SENSOR | EVSE_RCM_F_DC6 | EVSE_RCM_F_AC30);
}

ZTEST(evse_rcm, test_monitor_latches)
{
	/* SOLL-PROT-08: Auto-Reclose des Sensors loescht den Fehler nicht */
	lv.sw1 = true;
	zassert_true(evse_rcm_monitor(&rcm, &lv));
	lv.sw1 = false;
	zassert_false(evse_rcm_monitor(&rcm, &lv));
	zassert_equal(rcm.faults, EVSE_RCM_F_AC30);
}

ZTEST(evse_rcm, test_selftest_pass_bender_iec)
{
	/* Bender IEC: SWITCH_2 ~330 ms, SWITCH_1 ~770 ms nach Teststart des Sensors,
	 * Auto-Reclose ~500 ms nach Ende der Testwicklung (1,4 s) */
	rcm.faults = EVSE_RCM_F_DC6; /* alter Fehler wird durch Bestehen geloescht */
	zassert_true(evse_rcm_selftest_start(&rcm, &lv, now));
	run_test((struct sensor_model){.t_sw2 = 200 + 330, .t_sw1 = 200 + 770, .t_rel = 2300});
	zassert_equal(rcm.test, EVSE_RCM_TEST_PASSED);
	zassert_equal(rcm.faults, 0);
	zassert_false(rcm.out_test);
}

ZTEST(evse_rcm, test_selftest_pulse_timing)
{
	evse_rcm_selftest_start(&rcm, &lv, now);
	evse_ms_t start = now, on = -1, off = -1;

	while (rcm.test == EVSE_RCM_TEST_RUNNING && now - start < 400) {
		now += 1;
		evse_rcm_selftest_update(&rcm, &lv, now);
		if (rcm.out_test && on < 0) {
			on = now - start;
		}
		if (!rcm.out_test && on >= 0 && off < 0) {
			off = now - start;
		}
	}
	zassert_equal(on, 200);
	zassert_equal(off, 300);
}

ZTEST(evse_rcm, test_selftest_fail_points)
{
	/* M1: SWITCH_2 reagiert nicht */
	evse_rcm_selftest_start(&rcm, &lv, now);
	run_test((struct sensor_model){.t_sw2 = -1, .t_sw1 = 970, .t_rel = 2300});
	zassert_equal(rcm.test, EVSE_RCM_TEST_FAILED);
	zassert_equal(rcm.faults, EVSE_RCM_F_DC6 | EVSE_RCM_F_SELFTEST);

	/* M2: SWITCH_1 reagiert nicht */
	before(NULL);
	evse_rcm_selftest_start(&rcm, &lv, now);
	run_test((struct sensor_model){.t_sw2 = 530, .t_sw1 = -1, .t_rel = 2300});
	zassert_equal(rcm.faults, EVSE_RCM_F_AC30 | EVSE_RCM_F_SELFTEST);

	/* M3: beide bleiben ausgeloest */
	before(NULL);
	evse_rcm_selftest_start(&rcm, &lv, now);
	run_test((struct sensor_model){.t_sw2 = 530, .t_sw1 = 970, .t_rel = 99999});
	zassert_equal(rcm.faults, EVSE_RCM_F_SELFTEST);

	/* Rueckkehr zu spaet (einer bleibt bis nach 3,4 s) */
	before(NULL);
	evse_rcm_selftest_start(&rcm, &lv, now);
	evse_ms_t start = now;

	while (rcm.test == EVSE_RCM_TEST_RUNNING && now - start < 5000) {
		now += 5;
		int t = (int)(now - start);

		lv.sw2 = t >= 530 && t < 2000;
		lv.sw1 = t >= 970 && t < 3500;
		evse_rcm_selftest_update(&rcm, &lv, now);
	}
	zassert_equal(rcm.test, EVSE_RCM_TEST_FAILED);
	zassert_equal(rcm.faults, EVSE_RCM_F_SELFTEST);
}

ZTEST(evse_rcm, test_selftest_sensor_error)
{
	lv.err = true;
	zassert_false(evse_rcm_selftest_start(&rcm, &lv, now));
	zassert_true(rcm.faults & EVSE_RCM_F_SENSOR);

	before(NULL);
	evse_rcm_selftest_start(&rcm, &lv, now);
	now += 50;
	lv.err = true;
	evse_rcm_selftest_update(&rcm, &lv, now);
	zassert_equal(rcm.test, EVSE_RCM_TEST_FAILED);
	zassert_equal(rcm.faults, EVSE_RCM_F_SENSOR | EVSE_RCM_F_SELFTEST);
}

ZTEST(evse_rcm, test_selftest_abort)
{
	evse_rcm_selftest_start(&rcm, &lv, now);
	for (int i = 0; i < 50; i++) { /* 250 ms in 5-ms-Schritten */
		now += 5;
		evse_rcm_selftest_update(&rcm, &lv, now);
	}
	zassert_true(rcm.out_test);
	evse_rcm_selftest_abort(&rcm);
	zassert_false(rcm.out_test);
	zassert_equal(rcm.test, EVSE_RCM_TEST_IDLE);
	zassert_equal(rcm.faults, 0);
}

ZTEST(evse_rcm, test_monitor_silent_during_test)
{
	evse_rcm_selftest_start(&rcm, &lv, now);
	lv.sw2 = true;
	zassert_false(evse_rcm_monitor(&rcm, &lv));
	zassert_equal(rcm.faults, 0);
}

ZTEST_SUITE(evse_rcm, NULL, NULL, before, NULL, NULL);

/* ---- Schuetzueberwachung ---- */

static struct evse_ctm ctm;
static struct evse_ctm_in cin;

static void ctm_before(void *f)
{
	ARG_UNUSED(f);
	evse_ctm_init(&ctm);
	cin = (struct evse_ctm_in){.contactor_on = false, .off_since = 0,
				   .last_sense_edge = EVSE_MS_NONE, .ct_nc = true};
	now = 0;
}

static void ctm_run(enum evse_ctm_mode mode, int ms, bool sense_pulses)
{
	for (int i = 0; i < ms; i++) {
		now++;
		if (sense_pulses && (now % 20) == 0) {
			cin.last_sense_edge = now; /* 50 Hz hinter dem Schuetz */
		}
		evse_ctm_update(&ctm, mode, &cin, now);
	}
}

ZTEST(evse_ctm, test_sense_welded)
{
	ctm_run(EVSE_CTM_SENSE, 200 + 249, true);
	zassert_false(ctm.fault);
	ctm_run(EVSE_CTM_SENSE, 30, true);
	zassert_true(ctm.fault);
	zassert_false(evse_ctm_acknowledge(&ctm), "noch erkannt");
	ctm_run(EVSE_CTM_SENSE, 200, false);
	zassert_true(ctm.fault, "F14: kein automatisches Loeschen");
	zassert_true(evse_ctm_acknowledge(&ctm));
	zassert_false(ctm.fault);
}

ZTEST(evse_ctm, test_sense_ok_and_delay)
{
	/* Spannung in den ersten 200 ms nach Aus ist erlaubt */
	ctm_run(EVSE_CTM_SENSE, 190, true);
	ctm_run(EVSE_CTM_SENSE, 1000, false);
	zassert_false(ctm.fault);
	/* bei eingeschaltetem Schuetz keine Pruefung */
	cin.contactor_on = true;
	ctm_run(EVSE_CTM_SENSE, 2000, true);
	zassert_false(ctm.fault);
}

ZTEST(evse_ctm, test_no_nc_modes)
{
	cin.ct_no = true;
	ctm_run(EVSE_CTM_NO, 500, false);
	zassert_true(ctm.fault);
	ctm_before(NULL);
	cin.ct_nc = false;
	ctm_run(EVSE_CTM_NC, 500, false);
	zassert_true(ctm.fault);
	ctm_before(NULL);
	cin.ct_no = true;
	ctm_run(EVSE_CTM_OFF, 500, false);
	zassert_false(ctm.fault);
}

ZTEST(evse_ctm, test_meter_any_phase)
{
	/* E4: jede Phase einzeln (Original nur alle drei gemeinsam) */
	cin.meter_valid = true;
	cin.phase_v[1] = 230;
	ctm_run(EVSE_CTM_METER, 500, false);
	zassert_true(ctm.fault);
	zassert_false(evse_ctm_mode_valid(EVSE_CTM_METER, false));
	zassert_true(evse_ctm_mode_valid(EVSE_CTM_METER, true));
	zassert_false(evse_ctm_mode_valid(5, true));
}

ZTEST_SUITE(evse_ctm, NULL, NULL, ctm_before, NULL, NULL);
