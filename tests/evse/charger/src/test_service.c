/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Tests Bedienfunktionen fuer Shell/Schnittstelle: Selbsttest auf Anforderung
 * (SOLL-SH-07), Diagnose PE1 (SOLL-SH-12), Fehlerhistorie (SOLL-ERR-01), geordnetes Ende
 * vor Neustart (SOLL-RT-22).
 */
#include <zephyr/ztest.h>
#include <string.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/evse/charger.h>
#include "sim.h"

static struct sim s;
static struct evse_config cfg;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	sim_init(&s, &cfg);
}

ZTEST_SUITE(evse_service, NULL, NULL, before, NULL, NULL);

ZTEST(evse_service, test_selftest_command)
{
	sim_start(&s);
	zassert_false(evse_selftest_command(&s.e, s.now), "nicht in S0");
	sim_boot(&s);
	zassert_true(evse_selftest_command(&s.e, s.now));
	zassert_equal(s.e.rcm.test, EVSE_RCM_TEST_RUNNING);
	zassert_false(evse_selftest_command(&s.e, s.now), "laeuft schon");
	sim_run(&s, 4000);
	zassert_equal(s.e.rcm.test, EVSE_RCM_TEST_PASSED);
	zassert_equal(s.e.errors, 0);

	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	zassert_false(evse_selftest_command(&s.e, s.now), "nicht mit Fahrzeug");
}

ZTEST(evse_service, test_pe1_test)
{
	sim_start(&s);
	zassert_false(evse_pe1_test(&s.e, s.now, 1000), "nicht in S0");
	sim_boot(&s);
	zassert_false(s.e.out.pe1, "im Betrieb Low (F15)");
	zassert_false(evse_pe1_test(&s.e, s.now, 2001), "hoechstens 2 s");
	zassert_true(evse_pe1_test(&s.e, s.now, 1000));
	sim_run(&s, 500);
	zassert_true(s.e.out.pe1);
	sim_run(&s, 600);
	zassert_false(s.e.out.pe1, "nach 1 s wieder Low");

	/* Fahrzeug angesteckt: sofort Low, neuer Test abgelehnt */
	zassert_true(evse_pe1_test(&s.e, s.now, 2000));
	sim_run(&s, 100);
	zassert_true(s.e.out.pe1);
	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	zassert_false(s.e.out.pe1, "verlaesst S1/CP A");
	zassert_false(evse_pe1_test(&s.e, s.now, 1000), "nicht mit Fahrzeug");
}

ZTEST(evse_service, test_selftest_command_fails_with_bad_sensor)
{
	sim_boot(&s);
	s.rcm_healthy = false;
	zassert_true(evse_selftest_command(&s.e, s.now));
	sim_run(&s, 4000);
	zassert_equal(s.e.rcm.test, EVSE_RCM_TEST_FAILED);
	zassert_true(s.e.errors & EVSE_ERR_RCM_SELFTEST);
}

ZTEST(evse_service, test_error_history)
{
	sim_boot(&s);
	size_t n0 = evse_err_hist_count(&s.e);

	/* CP-Kurzschluss: ERR-CP-INVALID, geloescht, sobald der Pegel zurueck ist */
	s.car = EVSE_CP_E;
	sim_run(&s, 200);
	zassert_true(s.e.errors & EVSE_ERR_CP_INVALID);
	s.car = EVSE_CP_A;
	sim_run(&s, 3000);
	zassert_false(s.e.errors & EVSE_ERR_CP_INVALID);

	zassert_true(evse_err_hist_count(&s.e) >= n0 + 2);
	const struct evse_err_event *last = evse_err_hist_at(&s.e, 0);
	bool found_set = false;

	zassert_not_null(last);
	zassert_true(last->cleared & EVSE_ERR_CP_INVALID, "juengstes Ereignis: geloescht");
	for (size_t i = 0; i < evse_err_hist_count(&s.e); i++) {
		const struct evse_err_event *ev = evse_err_hist_at(&s.e, i);

		zassert_true(i == 0 || ev->t <= evse_err_hist_at(&s.e, i - 1)->t, "zeitlich sortiert");
		found_set |= (ev->set & EVSE_ERR_CP_INVALID) != 0;
	}
	zassert_true(found_set);
	zassert_is_null(evse_err_hist_at(&s.e, evse_err_hist_count(&s.e)));
}

ZTEST(evse_service, test_error_history_ring)
{
	sim_boot(&s);
	for (int i = 0; i < 40; i++) {
		s.car = EVSE_CP_E;
		sim_run(&s, 100);
		s.car = EVSE_CP_A;
		sim_run(&s, 100);
	}
	zassert_equal(evse_err_hist_count(&s.e), EVSE_ERR_HIST);
	zassert_true(evse_err_hist_at(&s.e, 0)->t > evse_err_hist_at(&s.e, EVSE_ERR_HIST - 1)->t);
}

ZTEST(evse_service, test_shutdown_ends_charging_orderly)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 5000) >= 0);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 1000) >= 0);
	zassert_true(s.e.out.contactor);

	evse_request_shutdown(&s.e);
	sim_run(&s, 1);
	zassert_equal(s.e.state, EVSE_S7_STOPPING, "geordnet, nicht hart");
	/* Fahrzeug reagiert auf PWM-Ende mit B */
	s.car = EVSE_CP_B;
	sim_run(&s, 500);
	zassert_true(evse_contactor_is_off(&s.e));
	zassert_equal(s.contactor_on_count, 1);
	sim_run(&s, 6000);
	zassert_equal(s.e.state, EVSE_S10_UNAVAILABLE);
	zassert_true(evse_contactor_is_off(&s.e));
}

ZTEST(evse_service, test_shutdown_vehicle_ignores_stop)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 5000) >= 0);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 1000) >= 0);
	evse_request_shutdown(&s.e);
	/* Fahrzeug bleibt in C: spaetestens nach 5 s Schuetz aus (E7) */
	sim_run(&s, EVSE_STOP_TIMEOUT_MS + 100);
	zassert_true(evse_contactor_is_off(&s.e));
}

ZTEST(evse_service, test_errors_json)
{
	static char buf[4096];
	char small[300];
	struct wf_jbuf b;

	sim_boot(&s);
	for (int i = 0; i < 20; i++) {
		s.car = EVSE_CP_E;
		sim_run(&s, 100);
		s.car = EVSE_CP_A;
		sim_run(&s, 100);
	}
	s.car = EVSE_CP_E;
	sim_run(&s, 100);

	wf_jb_init(&b, buf, sizeof(buf));
	api_json_errors(&s.e, s.now, &b);
	zassert_false(b.overflow);
	zassert_not_null(strstr(buf, "{\"active\":[\"CP-INVALID\"],\"seen\":[\"CP-INVALID\"]"), "%s", buf);
	zassert_not_null(strstr(buf, "\"history\":[{\"age_ms\":"));
	zassert_is_null(strstr(buf, "truncated"));

	/* kleiner Puffer: gekuerzt, aber gueltiges JSON-Ende */
	wf_jb_init(&b, small, sizeof(small));
	api_json_errors(&s.e, s.now, &b);
	zassert_false(b.overflow);
	zassert_true(strlen(small) > 0 && strcmp(small + strlen(small) - 19, "],\"truncated\":true}") == 0,
		     "%s", small);

	zassert_str_equal(api_err_name(2), "CP-INVALID");
	zassert_is_null(api_err_name(19));
	zassert_not_null(api_err_reset(9));
}
