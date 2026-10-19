/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-PP-01..06 */
#include <zephyr/ztest.h>
#include <wallfree/evse/pp.h>

#define BLOCK_MS 8

static struct evse_pp pp;
static struct evse_pp_config cfg;
static evse_ms_t now;

static void feed(int32_t mv, enum evse_cp_state cp, int dur_ms)
{
	for (int t = 0; t < dur_ms; t += BLOCK_MS) {
		now += BLOCK_MS;
		evse_pp_update(&pp, &cfg, mv, cp, now);
	}
}

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_pp_init(&pp);
	cfg = (struct evse_pp_config){.d1_proximity = true, .rotary_a = 16};
	now = 0;
}

ZTEST(evse_pp, test_classes)
{
	zassert_equal(evse_pp_classify(9000), 0);
	zassert_equal(evse_pp_classify(7401), 0);
	zassert_equal(evse_pp_classify(7400), 13);
	zassert_equal(evse_pp_classify(5331), 13);
	zassert_equal(evse_pp_classify(5330), 20);
	zassert_equal(evse_pp_classify(2821), 20);
	zassert_equal(evse_pp_classify(2820), 32);
	zassert_equal(evse_pp_classify(1521), 32);
	zassert_equal(evse_pp_classify(1520), 63);
	zassert_equal(evse_pp_classify(821), 63);
	zassert_equal(evse_pp_classify(820), EVSE_PP_INVALID);
}

ZTEST(evse_pp, test_hold_1s)
{
	/* SOLL-PP-03 */
	feed(2000, EVSE_CP_A, 1000);
	zassert_equal(pp.cable_a, 0);
	feed(2000, EVSE_CP_A, 16);
	zassert_equal(pp.cable_a, 32);
}

ZTEST(evse_pp, test_without_d1)
{
	/* SOLL-PP-01 */
	cfg.d1_proximity = false;
	feed(500, EVSE_CP_B, 16);
	zassert_equal(pp.cable_a, 16);
	zassert_false(pp.invalid);
	zassert_false(pp.reject);
}

ZTEST(evse_pp, test_invalid)
{
	/* SOLL-PP-04 */
	feed(500, EVSE_CP_A, 1100);
	zassert_true(pp.invalid);
	feed(9000, EVSE_CP_A, 1100);
	zassert_false(pp.invalid, "kein Kabel bei A ist kein Fehler");
	zassert_true(pp.unplugged_event || pp.cable_a == 0);
	feed(9000, EVSE_CP_B, 1100);
	zassert_true(pp.invalid, "kein Kabel bei erkanntem Fahrzeug");
	feed(9000, EVSE_CP_F, 1100);
	zassert_false(pp.invalid);
}

ZTEST(evse_pp, test_reject)
{
	/* SOLL-PP-05 */
	cfg.d2_reject = true;
	cfg.d3_only13 = true;
	feed(6000, EVSE_CP_B, 1100);
	zassert_equal(pp.cable_a, 13);
	zassert_true(pp.reject);
	feed(4000, EVSE_CP_B, 1100);
	zassert_equal(pp.cable_a, 20);
	zassert_false(pp.reject, "D3 an: nur 13 A abweisen");
	cfg.d3_only13 = false;
	feed(4000, EVSE_CP_B, 16);
	zassert_true(pp.reject, "D3 aus: auch 20 A abweisen");
	feed(2000, EVSE_CP_B, 1100);
	zassert_false(pp.reject);
	cfg.d2_reject = false;
	feed(6000, EVSE_CP_B, 1100);
	zassert_false(pp.reject);
}

ZTEST(evse_pp, test_unplug_event)
{
	/* SOLL-PP-06 */
	bool seen = false;

	feed(2000, EVSE_CP_A, 1100);
	for (int t = 0; t < 1100; t += BLOCK_MS) {
		now += BLOCK_MS;
		evse_pp_update(&pp, &cfg, 9000, EVSE_CP_A, now);
		seen |= pp.unplugged_event;
	}
	zassert_true(seen);
}

ZTEST_SUITE(evse_pp, NULL, NULL, before, NULL, NULL);
