/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Parameterverwaltung, Abschnitt 10, SOLL-PAR-01..03/40, E14 */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/evse/param.h>
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

static const struct evse_param *P(const char *name)
{
	const struct evse_param *p = evse_param_find(name);

	zassert_not_null(p, "%s", name);
	return p;
}

ZTEST(evse_param, test_table_consistent)
{
	size_t n = evse_param_count();

	zassert_true(n > 40);
	for (size_t i = 0; i < n; i++) {
		const struct evse_param *a = evse_param_at(i);

		zassert_not_null(a->name);
		zassert_not_null(a->help);
		zassert_not_null(a->unit);
		for (size_t j = i + 1; j < n; j++) {
			const struct evse_param *b = evse_param_at(j);

			zassert_true(strcmp(a->name, b->name) != 0, "Name doppelt: %s", a->name);
			zassert_not_equal(a->id, b->id, "Kennung doppelt: %u", a->id);
		}
		if (a->kind != EVSE_PK_STATUS) {
			zassert_true(a->min <= a->def && a->def <= a->max, "%s Default", a->name);
			zassert_true(a->get != NULL || a->base != 2, "%s", a->name);
		}
	}
	zassert_is_null(evse_param_at(n));
}

ZTEST(evse_param, test_defaults_match_factory)
{
	/* Tabelle == evse_config_defaults (E20) und Startwerte der Laufzeitwerte */
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	sim_init(&s, &cfg);
	sim_start(&s);
	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);

		if (p->kind == EVSE_PK_STATUS || p->base == 2) {
			continue;
		}
		zassert_equal(evse_param_get(&s.e, p), p->def, "%s: %d statt %d", p->name,
			      evse_param_get(&s.e, p), p->def);
	}
}

ZTEST(evse_param, test_bounds_every_param)
{
	/* min-1/min/max/max+1 fuer alle direkt gespeicherten Werte (SOLL-PAR-02) */
	cfg.meter_available = true; /* damit 4/1 erlaubt sind */
	sim_init(&s, &cfg);
	sim_boot(&s);
	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);

		if (p->kind == EVSE_PK_STATUS || p->base == 2) {
			continue;
		}
		int32_t old = evse_param_get(&s.e, p);

		zassert_equal(evse_param_set(&s.e, p, p->min - 1, s.now), EVSE_PARAM_E_RANGE, "%s",
			      p->name);
		zassert_equal(evse_param_set(&s.e, p, p->max + 1, s.now), EVSE_PARAM_E_RANGE, "%s",
			      p->name);
		zassert_equal(evse_param_get(&s.e, p), old, "%s unveraendert", p->name);
		zassert_equal(evse_param_set(&s.e, p, p->max, s.now), EVSE_PARAM_OK, "%s",
			      p->name);
		zassert_equal(evse_param_get(&s.e, p), p->max, "%s", p->name);
		zassert_equal(evse_param_set(&s.e, p, p->def, s.now), EVSE_PARAM_OK, "%s",
			      p->name);
	}
}

ZTEST(evse_param, test_special_ranges)
{
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "external_current", 5999, s.now),
		      EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "external_current", 1, s.now),
		      EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "external_current", 0, s.now), EVSE_PARAM_OK);
	zassert_equal(evse_param_set_by_name(&s.e, "external_current", 6000, s.now),
		      EVSE_PARAM_OK);
	zassert_equal(s.e.rt.external_ma, 6000);
	zassert_equal(evse_param_set_by_name(&s.e, "wakeup_delay", 4999, s.now),
		      EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "wakeup_delay", 0, s.now), EVSE_PARAM_OK);
	zassert_equal(evse_param_set_by_name(&s.e, "in_function_en", 23, s.now),
		      EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "out_function_er", 43, s.now),
		      EVSE_PARAM_E_RANGE);
}

ZTEST(evse_param, test_meter_dependent)
{
	/* F1: ohne Messquelle nicht verfuegbar */
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "contactor_monitor", 4, s.now),
		      EVSE_PARAM_E_UNAVAILABLE);
	zassert_equal(evse_param_set_by_name(&s.e, "overcurrent_enable", 1, s.now),
		      EVSE_PARAM_E_UNAVAILABLE);
	zassert_equal(evse_param_set_by_name(&s.e, "overcurrent_enable", 0, s.now),
		      EVSE_PARAM_OK);
	zassert_equal(evse_param_set_by_name(&s.e, "contactor_monitor", 3, s.now), EVSE_PARAM_OK);
}

ZTEST(evse_param, test_readonly_unknown)
{
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "state", 1, s.now), EVSE_PARAM_E_READONLY);
	zassert_equal(evse_param_set_by_name(&s.e, "errors", 0, s.now), EVSE_PARAM_E_READONLY);
	zassert_equal(evse_param_set_by_name(&s.e, "gibtsnicht", 0, s.now), EVSE_PARAM_E_UNKNOWN);
	zassert_is_null(evse_param_find("gibtsnicht"));
	for (int err = 0; err <= EVSE_PARAM_E_UNAVAILABLE + 1; err++) {
		zassert_true(strlen(evse_param_strerror(err)) > 0);
	}
}

ZTEST(evse_param, test_status_values)
{
	sim_boot(&s);
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 4000);
	sim_run(&s, 50);
	zassert_equal(evse_param_get(&s.e, P("state")), EVSE_S5_PWM_ON);
	zassert_equal(evse_param_get(&s.e, P("cp_status")), EVSE_CP_B);
	zassert_equal(evse_param_get(&s.e, P("cp_duty")), 266);
	zassert_equal(evse_param_get(&s.e, P("current")), 16000);
	zassert_equal(evse_param_get(&s.e, P("allowed_charging_current")), 15960);
	zassert_equal(evse_param_get(&s.e, P("cp_pos")), 9000);
	zassert_equal(evse_param_get(&s.e, P("rotary")), 4);
	zassert_equal(evse_param_get(&s.e, P("errors")), 0);
	/* jeder Parameter ist lesbar (SOLL-PAR-01, SOLL-API-30) */
	for (size_t i = 0; i < evse_param_count(); i++) {
		(void)evse_param_get(&s.e, evse_param_at(i));
	}
	zassert_equal(evse_param_get(&s.e, P("release")), 1);
	zassert_equal(evse_param_get(&s.e, P("permission")), 1);
	zassert_equal(evse_param_get(&s.e, P("rcm_selftest")), EVSE_RCM_TEST_PASSED);
	zassert_equal(evse_param_get(&s.e, P("outputs")), (1 << EVSE_DOUT_CR) | (1 << EVSE_DOUT_LR),
		      "CR = PWM an, LR = Verriegelungsanforderung");
}

ZTEST(evse_param, test_hardware_limit_only_lower)
{
	/* SOLL-PAR-40 */
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "hardware_limit_override", 20000, s.now),
		      EVSE_PARAM_E_RANGE, "hoeher als Drehschalter 16 A");
	zassert_equal(evse_param_set_by_name(&s.e, "hardware_limit_override", 5000, s.now),
		      EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "hardware_limit_override", 10000, s.now),
		      EVSE_PARAM_OK);
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 4000);
	sim_run(&s, 1);
	zassert_equal(s.e.out.cp_duty, 166);
	zassert_equal(evse_param_set_by_name(&s.e, "hardware_limit_override", 0, s.now),
		      EVSE_PARAM_OK);
	sim_run(&s, 10);
	zassert_equal(s.e.out.cp_duty, 266);
}

ZTEST(evse_param, test_dip_override_only_s1)
{
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "dip_override", DIP(7), s.now), EVSE_PARAM_OK);
	s.car = EVSE_CP_B;
	sim_run(&s, 4000);
	zassert_equal(s.e.state, EVSE_S3_WAIT, "D7 ohne EN: keine Freigabe");
	zassert_equal(evse_param_set_by_name(&s.e, "dip_override", 0, s.now), EVSE_PARAM_E_STATE);
	zassert_equal(s.e.cfg.dip, DIP(7));
}

ZTEST(evse_param, test_lock_state_triggers_cycle)
{
	/* SOLL-PAR-40: echter Zyklus statt Merker */
	cfg.dip = DIP(4) | DIP(6);
	cfg.din.code[EVSE_DIN_LD] = EVSE_IN_LD_PULSED;
	sim_init(&s, &cfg);
	sim_boot(&s);
	zassert_equal(evse_param_set_by_name(&s.e, "lock_state", 2, s.now), EVSE_PARAM_E_RANGE);
	zassert_equal(evse_param_set_by_name(&s.e, "lock_state", 1, s.now), EVSE_PARAM_OK);
	zassert_equal(evse_param_get(&s.e, P("lock_state")), EVSE_LOCK_LOCKING);
	sim_run(&s, 3000);
	zassert_true(s.actuator_locked, "Aktor wurde wirklich angesteuert");
	s.car = EVSE_CP_B;
	sim_until_state(&s, EVSE_S5_PWM_ON, 6000);
	zassert_equal(evse_param_set_by_name(&s.e, "lock_state", 0, s.now), EVSE_PARAM_E_STATE);
}

ZTEST(evse_param, test_config_effect_immediate)
{
	/* SOLL-PAR-03: LD-Funktion entfernen bei D4+D6 -> Konfigurationsfehler */
	cfg.dip = DIP(4) | DIP(6);
	sim_init(&s, &cfg);
	s.ld_model = LD_MODEL_PERMANENT;
	sim_boot(&s);
	zassert_equal(s.e.errors & EVSE_ERR_CONFIG, 0);
	zassert_equal(evse_param_set_by_name(&s.e, "in_function_ld", 0, s.now), EVSE_PARAM_OK);
	zassert_true(s.e.errors & EVSE_ERR_CONFIG);
	zassert_equal(evse_param_set_by_name(&s.e, "in_function_ld", 22, s.now), EVSE_PARAM_OK);
	zassert_equal(s.e.errors & EVSE_ERR_CONFIG, 0);
	zassert_equal(s.e.ld_mode, EVSE_LD_PULSED);
}

ZTEST(evse_param, test_restore)
{
	/* SOLL-BOOT-04: gespeicherte Werte mit Pruefung uebernehmen */
	struct evse_config c;

	evse_config_defaults(&c);
	zassert_equal(evse_param_restore(&c, 525, 1500), EVSE_PARAM_OK);
	zassert_equal(c.lock_pulse_ms, 1500);
	zassert_equal(evse_param_restore(&c, 525, 9999), EVSE_PARAM_E_RANGE);
	zassert_equal(c.lock_pulse_ms, 1500, "ungueltig -> alter Wert bleibt");
	zassert_equal(evse_param_restore(&c, 390, 4), EVSE_PARAM_E_UNAVAILABLE);
	zassert_equal(evse_param_restore(&c, 528, 10000), EVSE_PARAM_E_READONLY,
		      "Laufzeitwert wird nicht gespeichert");
	zassert_equal(evse_param_restore(&c, 9999, 1), EVSE_PARAM_E_UNKNOWN);
	zassert_equal(evse_param_restore(&c, 1004, 0), EVSE_PARAM_OK);
	zassert_equal(c.external_current_default_ma, 0);
}

ZTEST(evse_param, test_startup_defaults_from_config)
{
	/* F12: Startwert kommt aus der Konfiguration */
	cfg.external_current_default_ma = 0;
	sim_init(&s, &cfg);
	sim_start(&s);
	zassert_equal(s.e.rt.external_ma, 0);
	zassert_is_null(evse_param_find("enable_register"), "F37");
	zassert_is_null(evse_param_by_id(400), "F37: Feld 400 nicht mehr");
}

ZTEST_SUITE(evse_param, NULL, NULL, before, NULL, NULL);
