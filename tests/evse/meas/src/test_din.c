/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-IO-10..12, Abschnitt 9.1 */
#include <zephyr/ztest.h>
#include <wallfree/evse/din.h>

#define BLOCK_MS 8

static struct evse_din din;
static struct evse_din_config cfg;
static int32_t mv[EVSE_DIN_COUNT];
static evse_ms_t now;

static void step(int n)
{
	for (int i = 0; i < n; i++) {
		now += BLOCK_MS;
		evse_din_update(&din, &cfg, mv, now);
	}
}

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_din_init(&din);
	/* Werkseinstellung E20: LD 3, EN 1, ML 4, XR 2, IN 11 */
	cfg = (struct evse_din_config){.code = {3, 1, 4, 2, 11}};
	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		mv[i] = 0;
	}
	now = 0;
}

ZTEST(evse_din, test_hysteresis_and_debounce)
{
	mv[EVSE_DIN_EN] = 12000;
	step(1);
	zassert_true(din.ch[EVSE_DIN_EN].hyst);
	zassert_false(din.en, "erst nach 2 Bloecken (F2)");
	step(1);
	zassert_true(din.en);
	/* Zwischenbereich haelt den Wert */
	mv[EVSE_DIN_EN] = 6000;
	step(5);
	zassert_true(din.en);
	mv[EVSE_DIN_EN] = 9000; /* nicht > 9000 */
	step(5);
	zassert_true(din.en);
	mv[EVSE_DIN_EN] = 2999;
	step(2);
	zassert_false(din.en);
}

ZTEST(evse_din, test_single_block_glitch)
{
	mv[EVSE_DIN_EN] = 12000;
	step(1);
	mv[EVSE_DIN_EN] = 0;
	step(1);
	zassert_false(din.en);
}

ZTEST(evse_din, test_limits_min_and_or)
{
	/* SOLL-CUR-03 kleinster aktiver Code; F26 ODER */
	cfg.code[EVSE_DIN_IN] = EVSE_IN_MAX_20A;
	cfg.code[EVSE_DIN_XR] = EVSE_IN_MAX_10A;
	mv[EVSE_DIN_IN] = 12000;
	step(2);
	zassert_equal(din.limit_ma, 20000);
	mv[EVSE_DIN_XR] = 12000;
	step(2);
	zassert_equal(din.limit_ma, 10000);

	cfg.code[EVSE_DIN_IN] = EVSE_IN_EN;
	mv[EVSE_DIN_EN] = 0;
	step(1);
	zassert_true(din.en, "EN auf zwei Klemmen: ODER");
}

ZTEST(evse_din, test_en_portal_like_en)
{
	cfg.code[EVSE_DIN_EN] = EVSE_IN_EN_PORTAL;
	mv[EVSE_DIN_EN] = 12000;
	step(2);
	zassert_true(din.en);
}

ZTEST(evse_din, test_en_pulsed_toggle_200ms)
{
	cfg.code[EVSE_DIN_EN] = EVSE_IN_EN_PULSED;
	mv[EVSE_DIN_EN] = 12000;
	step(2);
	zassert_true(din.en);
	mv[EVSE_DIN_EN] = 0;
	step(2);
	zassert_true(din.en, "Umschalten nur bei steigender Flanke");
	mv[EVSE_DIN_EN] = 12000;
	step(2); /* zweite Flanke nach 32 ms: gesperrt */
	zassert_true(din.en);
	mv[EVSE_DIN_EN] = 0;
	step(30);
	mv[EVSE_DIN_EN] = 12000;
	step(2);
	zassert_false(din.en);
}

ZTEST(evse_din, test_ml_events)
{
	mv[EVSE_DIN_ML] = 12000;
	step(1);
	zassert_equal(din.ml_event, EVSE_ML_EV_NONE);
	step(1);
	zassert_equal(din.ml_event, EVSE_ML_EV_SET1);
	step(1);
	zassert_equal(din.ml_event, EVSE_ML_EV_NONE);
	mv[EVSE_DIN_ML] = 0;
	step(2);
	zassert_equal(din.ml_event, EVSE_ML_EV_SET0);

	cfg.code[EVSE_DIN_ML] = EVSE_IN_ML_PULSED;
	mv[EVSE_DIN_ML] = 12000;
	step(2);
	zassert_equal(din.ml_event, EVSE_ML_EV_TOGGLE);
}

ZTEST(evse_din, test_ld_source)
{
	int ch;

	zassert_equal(evse_din_ld_source(&cfg, &ch), EVSE_LD_PERMANENT);
	zassert_equal(ch, EVSE_DIN_LD);
	cfg.code[EVSE_DIN_IN] = EVSE_IN_LD_PULSED;
	zassert_equal(evse_din_ld_source(&cfg, &ch), EVSE_LD_PULSED);
	zassert_equal(ch, EVSE_DIN_IN);
	cfg.code[EVSE_DIN_IN] = 0;
	cfg.code[EVSE_DIN_LD] = 0;
	zassert_equal(evse_din_ld_source(&cfg, &ch), EVSE_LD_NONE);
	zassert_equal(ch, -1);
}

ZTEST(evse_din, test_code_valid)
{
	zassert_true(evse_in_code_valid(0));
	zassert_true(evse_in_code_valid(22));
	zassert_false(evse_in_code_valid(23));
	zassert_false(evse_in_code_valid(-1));
}

ZTEST_SUITE(evse_din, NULL, NULL, before, NULL, NULL);
