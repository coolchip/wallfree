/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Abbildung Ladelogik -> API (12.1, SOLL-API-03..05/30) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/evse_api.h>
#include "sim.h"

static struct sim s;
static struct evse_config cfg;
static char buf[2048];
static struct wf_jbuf jb;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	sim_init(&s, &cfg);
	wf_jb_init(&jb, buf, sizeof(buf));
}

ZTEST_SUITE(evse_api, NULL, NULL, before, NULL, NULL);

ZTEST(evse_api, test_state_idle)
{
	sim_boot(&s);
	api_json_state(&s.e, &jb);
	zassert_str_equal(buf, "{\"iec61851_state\":0,\"charger_state\":0,\"contactor_state\":0,"
			       "\"contactor_error\":0,\"allowed_charging_current\":0,"
			       "\"error_state\":0,\"lock_state\":1}", "%s", buf);
}

ZTEST(evse_api, test_state_charging_sequence)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	zassert_true(sim_until_state(&s, EVSE_S5_PWM_ON, 10000) >= 0);
	zassert_equal(api_iec61851_state(&s.e), 1);
	zassert_equal(api_charger_state(&s.e), 2);
	zassert_true(s.e.cur.allowed_ma >= 6000);

	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 5000) >= 0);
	sim_run(&s, 200);
	zassert_equal(api_iec61851_state(&s.e), 2);
	zassert_equal(api_charger_state(&s.e), 3);
	zassert_equal(api_contactor_state(&s.e), 3);
	zassert_equal(api_error_state(&s.e), API_ERR_OK);

	/* evcc: Status() akzeptiert nur 0..2 */
	zassert_true(api_iec61851_state(&s.e) < 3);
}

ZTEST(evse_api, test_error_mapping)
{
	sim_boot(&s);
	s.e.errors = EVSE_ERR_CONFIG;
	zassert_equal(api_error_state(&s.e), API_ERR_SWITCH);
	s.e.errors = EVSE_ERR_LOCK;
	zassert_equal(api_error_state(&s.e), API_ERR_OTHER);
	s.e.errors = EVSE_ERR_CONTACTOR | EVSE_ERR_CONFIG;
	zassert_equal(api_error_state(&s.e), API_ERR_CONTACTOR);
	s.e.contactor = false;
	zassert_equal(api_contactor_error(&s.e), 6, "Schuetz aus, Fehler: geklebt");
	s.e.contactor = true;
	zassert_equal(api_contactor_error(&s.e), 2);
	s.e.errors = EVSE_ERR_RCM_DC6 | EVSE_ERR_CONTACTOR;
	zassert_equal(api_error_state(&s.e), API_ERR_RCM, "Fehlerstrom hat Vorrang");
	s.e.errors = 0;
	zassert_equal(api_contactor_error(&s.e), 0);
}

ZTEST(evse_api, test_iec_states)
{
	static const int map[][2] = {
		{EVSE_CP_A, 0}, {EVSE_CP_B, 1}, {EVSE_CP_C, 2}, {EVSE_CP_D, 3}, {EVSE_CP_E, 4},
		{EVSE_CP_F, 4},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		s.e.status = map[i][0];
		zassert_equal(api_iec61851_state(&s.e), map[i][1]);
	}
	static const int cs[][2] = {
		{EVSE_S0_INIT, 0}, {EVSE_S1_IDLE, 0}, {EVSE_S2_LOCKING, 1}, {EVSE_S3_WAIT, 1},
		{EVSE_S4_SELFTEST, 2}, {EVSE_S5_PWM_ON, 2}, {EVSE_S6_CHARGING, 3},
		{EVSE_S7_STOPPING, 3}, {EVSE_S9_ERROR, 4}, {EVSE_S10_UNAVAILABLE, 4},
	};
	for (size_t i = 0; i < ARRAY_SIZE(cs); i++) {
		s.e.state = cs[i][0];
		zassert_equal(api_charger_state(&s.e), cs[i][1], "S%d", cs[i][0]);
	}
	s.e.state = EVSE_S8_UNLOCKING;
	s.e.status = EVSE_CP_A;
	zassert_equal(api_charger_state(&s.e), 0);
	s.e.status = EVSE_CP_B;
	zassert_equal(api_charger_state(&s.e), 1);
}

ZTEST(evse_api, test_external_current)
{
	int32_t ma;

	sim_boot(&s);
	api_json_external_current(&s.e, &jb);
	zassert_str_equal(buf, "{\"current\":80000}");

	zassert_equal(api_parse_current("{\"current\": 16000}", 18, &ma), 0);
	zassert_equal(ma, 16000);
	zassert_equal(api_parse_current("16000", 5, &ma), 0, "WARP-Kurzform");
	zassert_equal(api_parse_current("{\"current\":0}", 13, &ma), 0);
	zassert_equal(ma, 0);
	zassert_equal(api_parse_current("{\"curent\":1}", 12, &ma), -1);
	zassert_equal(api_parse_current("{\"current\":\"6000\"}", 18, &ma), -1);
	zassert_equal(api_parse_current("{}", 2, &ma), -1);
	zassert_equal(api_parse_current("{\"current\":99999999999}", 23, &ma), -1);
}

static int written;
static void on_written(void *ctx, const struct evse_param *p, int32_t v)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(p);
	ARG_UNUSED(v);
	written++;
}

ZTEST(evse_api, test_set_params)
{
	const char *ok = "{\"lock_pulse\": 800, \"external_current\": 6000, \"reject_state_d\": false}";
	const char *mixed = "{\"lock_pulse\": 50, \"unlock_pulse\": 900, \"nix\": 1, "
			    "\"state\": 1, \"wakeup_delay\": \"x\"}";

	sim_boot(&s);
	written = 0;
	zassert_equal(api_set_params(&s.e, ok, strlen(ok), s.now, on_written, NULL, &jb), 200);
	zassert_str_equal(buf, "{}");
	zassert_equal(written, 3);
	zassert_equal(s.e.cfg.lock_pulse_ms, 800);
	zassert_equal(s.e.rt.external_ma, 6000);
	zassert_false(s.e.cfg.reject_state_d);

	wf_jb_init(&jb, buf, sizeof(buf));
	written = 0;
	zassert_equal(api_set_params(&s.e, mixed, strlen(mixed), s.now, on_written, NULL, &jb),
		      400);
	zassert_equal(written, 1, "nur unlock_pulse");
	zassert_equal(s.e.cfg.unlock_pulse_ms, 900);
	zassert_equal(s.e.cfg.lock_pulse_ms, 800);
	zassert_not_null(strstr(buf, "\"lock_pulse\":\"Wert außerhalb des zulässigen Bereichs\""));
	zassert_not_null(strstr(buf, "\"nix\":\"unbekannter Parameter\""));
	zassert_not_null(strstr(buf, "\"state\":\"nur lesbar\""));
	zassert_not_null(strstr(buf, "\"wakeup_delay\":\"Zahl erwartet\""));

	/* Syntaxfehler am Ende: nichts angewendet */
	const char *broken = "{\"lock_pulse\": 1000, ";

	wf_jb_init(&jb, buf, sizeof(buf));
	zassert_equal(api_set_params(&s.e, broken, strlen(broken), s.now, NULL, NULL, &jb), 400);
	zassert_equal(s.e.cfg.lock_pulse_ms, 800);
	zassert_not_null(strstr(buf, "\"error\""));
}

ZTEST(evse_api, test_params_and_info)
{
	static char big[16384];
	char part[700];
	struct wf_jbuf pj;
	size_t idx = 0;
	int parts = 0;

	sim_boot(&s);
	api_json_params(&s.e, &jb);
	zassert_false(jb.overflow, "alle Werte passen in 2 KB");
	zassert_not_null(strstr(buf, "\"lock_pulse\":500"));
	zassert_not_null(strstr(buf, "\"state\":1"));

	/* Beschreibung in Teilen wie im HTTP-Server */
	big[0] = '\0';
	while (idx <= evse_param_count()) {
		wf_jb_init(&pj, part, sizeof(part));
		size_t next = api_json_param_info(idx, &pj);

		zassert_true(next > idx || pj.len > 0, "Fortschritt");
		zassert_true(strlen(big) + pj.len < sizeof(big));
		strcat(big, part);
		idx = next;
		parts++;
	}
	zassert_true(parts > 3);
	zassert_equal(big[0], '[');
	zassert_equal(big[strlen(big) - 1], ']');
	zassert_not_null(strstr(big, "{\"name\":\"lock_pulse\",\"kind\":\"config\",\"unit\":\"ms\","
				     "\"min\":100,\"max\":3000,\"default\":500,\"field\":525,"));
	zassert_not_null(strstr(big, "{\"name\":\"state\",\"kind\":\"status\""));
}

/* GET /evse/limits: Sollwert, Grenzen und welche Grenze wirkt (Drehschalter Default 16 A) */
ZTEST(evse_api, test_limits)
{
	sim_boot(&s);
	s.car = EVSE_CP_C;
	zassert_true(sim_until_state(&s, EVSE_S6_CHARGING, 10000) >= 0);
	api_json_limits(&s.e, &jb);
	zassert_str_equal(buf, "{\"external\":80000,\"external_ignored\":false,\"hardware\":16000,"
			       "\"cable\":0,\"input\":0,\"effective\":16000,\"allowed\":15960,"
			       "\"limited_by\":\"hardware\"}", "%s", buf);

	s.e.rt.external_ma = 10000;
	sim_run(&s, 20);
	zassert_str_equal(api_limited_by(&s.e), "external");
	s.e.rt.external_ma = 16000; /* Gleichstand: Sollwert wirkt */
	sim_run(&s, 20);
	zassert_str_equal(api_limited_by(&s.e), "external");
	s.e.rt.external_ma = 32000;
	s.e.rt.hardware_limit_ma = 8000; /* 801 absenken */
	sim_run(&s, 20);
	zassert_str_equal(api_limited_by(&s.e), "hardware");
	zassert_equal(s.e.cur_in.hardware_ma, 8000);
	s.e.rt.external_ma = 0;
	sim_run(&s, 20);
	zassert_str_equal(api_limited_by(&s.e), "blocked");
	wf_jb_init(&jb, buf, sizeof(buf));
	api_json_limits(&s.e, &jb);
	zassert_not_null(strstr(buf, "\"effective\":0,"), "%s", buf);
}
