/* SPDX-License-Identifier: Apache-2.0 */
/* Wahrheitstabelle der Ausgangsfunktionen 0..42 (Tabelle 9.3) */
#include <zephyr/ztest.h>
#include <wallfree/evse/charger.h>

static struct evse e;

struct sit {
	enum evse_cp_state st;
	bool err;
	bool pwm;
};

static void setup(struct sit x)
{
	struct evse_config cfg;

	evse_config_defaults(&cfg);
	evse_init(&e, &cfg, EVSE_LOCK_UNLOCKED, 0);
	e.state = EVSE_S5_PWM_ON;
	e.status = x.st;
	e.errors = x.err ? EVSE_ERR_INPUT : 0;
	e.cp_duty = x.pwm ? 266 : EVSE_DUTY_OFF;
}

static bool ev(uint8_t code)
{
	return evse_dout_eval(&e, code, 100000);
}

/* Erwartung je Code fuer die Situationen A, B, B+PWM, C, D, E, F (ohne Fehler)
 * und A, B, C mit Fehler; Spaltenreihenfolge wie sits[] */
static const struct sit sits[] = {
	{EVSE_CP_A, false, false}, {EVSE_CP_B, false, false}, {EVSE_CP_B, false, true},
	{EVSE_CP_C, false, true},  {EVSE_CP_D, false, true},  {EVSE_CP_E, false, false},
	{EVSE_CP_F, false, false}, {EVSE_CP_A, true, false},  {EVSE_CP_B, true, true},
	{EVSE_CP_C, true, true},   {EVSE_CP_F, true, false},
};

#define N ARRAY_SIZE(sits)

static const struct {
	uint8_t code;
	const char *exp; /* je Situation '1' oder '0' */
} table[] = {
	/*          A B Bp C D E F Ae Be Ce Fe */
	{0, "00000000000"},
	{1, "10000000000"},
	{2, "01100000000"},
	{3, "00100000000"},
	{4, "01000000000"},
	{5, "00010000010"},
	{6, "00001000000"},
	{7, "00000101110"},
	{8, "00000010001"},
	{9, "11100000000"},
	{10, "10100000000"},
	{11, "11000000000"},
	{12, "11110000010"},
	{13, "11101000000"},
	{14, "11111000010"},
	{15, "00000111111"},
	{16, "00011000010"},
	{17, "00111000110"},
	{37, "01111000010"},
	{41, "11000000000"},
};

ZTEST(evse_outputs, test_state_codes)
{
	for (size_t t = 0; t < ARRAY_SIZE(table); t++) {
		for (size_t i = 0; i < N; i++) {
			setup(sits[i]);
			bool want = table[t].exp[i] == '1';

			zassert_equal(ev(table[t].code), want, "Code %u Situation %u",
				      table[t].code, (unsigned)i);
		}
	}
}

ZTEST(evse_outputs, test_cable_codes)
{
	static const int16_t cables[] = {0, 13, 20, 32, 63, -1};
	/* Codes 18..25 je Kabelwert */
	static const char *exp[] = {
		/* 0 13 20 32 63 -1 */
		"011110", /* 18 */
		"100001", /* 19 */
		"010000", /* 20 */
		"001000", /* 21 */
		"000100", /* 22 */
		"000010", /* 23 */
		"011000", /* 24 */
		"011100", /* 25 */
	};

	for (int c = 18; c <= 25; c++) {
		for (size_t i = 0; i < ARRAY_SIZE(cables); i++) {
			setup(sits[1]);
			e.pp.cable_a = cables[i];
			zassert_equal(ev(c), exp[c - 18][i] == '1', "Code %d Kabel %d", c,
				      cables[i]);
		}
	}
}

ZTEST(evse_outputs, test_flag_codes)
{
	setup(sits[3]);
	zassert_false(ev(26));
	e.errors = EVSE_ERR_CABLE_REJECT;
	zassert_true(ev(26));

	setup(sits[3]);
	zassert_false(ev(27));
	e.contactor = true;
	zassert_true(ev(27));

	setup(sits[4]);
	zassert_false(ev(28), "D ohne Laden");
	e.state = EVSE_S6_CHARGING;
	zassert_true(ev(28));

	setup(sits[0]);
	zassert_false(ev(29));
	e.last_lock_wish = true;
	zassert_true(ev(29));

	for (int i = 0; i < 4; i++) {
		setup(sits[0]);
		zassert_false(ev(30 + i));
		e.rt.register_output[i] = true;
		zassert_true(ev(30 + i));
	}

	setup(sits[3]);
	e.errors = EVSE_ERR_OVERCURRENT;
	zassert_true(ev(34));
	zassert_false(ev(35));
	e.errors = EVSE_ERR_CONTACTOR;
	zassert_true(ev(35));
	e.errors = EVSE_ERR_STATE_D;
	zassert_true(ev(36));
}

ZTEST(evse_outputs, test_special_codes)
{
	/* 38: Code-19-Eingang bei A/B, Nachlauf 1 s */
	setup(sits[0]);
	zassert_false(ev(38));
	e.din.max_current = true;
	zassert_true(ev(38));
	setup(sits[3]);
	e.din.max_current = true;
	e.contactor = false;
	e.contactor_off_since = 99500;
	zassert_true(ev(38), "Nachlauf");
	e.contactor_off_since = 98000;
	zassert_false(ev(38));

	/* 39: Freigabe und (C/D oder B nach Sperre) */
	setup(sits[3]);
	zassert_false(ev(39));
	e.release = true;
	zassert_true(ev(39));
	setup(sits[1]);
	e.release = true;
	zassert_false(ev(39));
	e.was_blocked = true;
	zassert_true(ev(39));

	/* 40: Blinken in S0, danach wie 9 */
	setup(sits[0]);
	e.state = EVSE_S0_INIT;
	zassert_true(evse_dout_eval(&e, 40, 100200));
	zassert_false(evse_dout_eval(&e, 40, 100700));
	e.state = EVSE_S1_IDLE;
	zassert_true(evse_dout_eval(&e, 40, 100700));

	/* 42: pausiert nach Laden */
	setup(sits[2]);
	zassert_false(ev(42));
	e.charged_this_plug = true;
	zassert_true(ev(42));

	/* unbekannt */
	setup(sits[0]);
	zassert_false(ev(43));
	zassert_false(ev(255));
}

ZTEST_SUITE(evse_outputs, NULL, NULL, NULL, NULL, NULL);
