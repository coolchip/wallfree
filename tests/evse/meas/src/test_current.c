/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-CUR-01..22 und Drehschalter */
#include <zephyr/ztest.h>
#include <wallfree/evse/current.h>
#include <wallfree/evse/switches.h>

static struct evse_current_in in;
static struct evse_current_out out;

static void before(void *f)
{
	ARG_UNUSED(f);
	in = (struct evse_current_in){.hardware_ma = 16000, .external_ma = 80000};
}

ZTEST(evse_current, test_duty_examples)
{
	/* SOLL-CUR-20 Beispiele */
	zassert_equal(evse_duty_from_ma(6000), 100);
	zassert_equal(evse_duty_from_ma(16000), 266);
	zassert_equal(evse_duty_from_ma(32000), 533);
	zassert_equal(evse_duty_from_ma(51000), 850);
	zassert_equal(evse_duty_from_ma(51001), 850);
	zassert_equal(evse_duty_from_ma(52499), 850);
	zassert_equal(evse_duty_from_ma(52500), 850);
	zassert_equal(evse_duty_from_ma(52750), 851);
	zassert_equal(evse_duty_from_ma(80000), 960);
	zassert_equal(evse_duty_from_ma(5999), EVSE_DUTY_OFF);
}

ZTEST(evse_current, test_duty_never_exceeds)
{
	/* Kern von SOLL-CUR-20: Strom des Tastgrads <= Sollstrom, und maximal */
	for (int32_t ma = 6000; ma <= 80000; ma += 1) {
		uint16_t d = evse_duty_from_ma(ma);

		zassert_true(evse_ma_from_duty(d) <= ma, "ma=%d d=%u", ma, d);
		if (d < 960) {
			zassert_true(evse_ma_from_duty(d + 1) > ma, "nicht maximal: ma=%d", ma);
		}
	}
}

ZTEST(evse_current, test_ma_from_duty_ranges)
{
	/* HB S. 39 Tab. 6-3 */
	zassert_equal(evse_ma_from_duty(0), 0);
	zassert_equal(evse_ma_from_duty(50), 0, "5 %: digitale Kommunikation");
	zassert_equal(evse_ma_from_duty(79), 0);
	zassert_equal(evse_ma_from_duty(80), 6000);
	zassert_equal(evse_ma_from_duty(99), 6000);
	zassert_equal(evse_ma_from_duty(100), 6000);
	zassert_equal(evse_ma_from_duty(850), 51000);
	zassert_equal(evse_ma_from_duty(851), 52750);
	zassert_equal(evse_ma_from_duty(960), 80000);
	zassert_equal(evse_ma_from_duty(965), 80000);
	zassert_equal(evse_ma_from_duty(970), 80000);
	zassert_equal(evse_ma_from_duty(980), 85000);
	zassert_equal(evse_ma_from_duty(1000), 0, "100 %: keine PWM");
	zassert_equal(evse_duty_from_ma(100000), 960, "auf 80 A begrenzt");
	zassert_equal(evse_duty_from_ma(-5), EVSE_DUTY_OFF);
}

ZTEST(evse_current, test_implausible)
{
	/* SOLL-CUR-13: Slot ueber hardware kann nur durch Fehler entstehen */
	in.hardware_ma = 90000;
	in.external_ma = 90000;
	evse_current_eval(&in, &out);
	zassert_true(out.implausible);
	zassert_true(out.blocked);
	zassert_equal(out.duty, EVSE_DUTY_OFF);
	in.hardware_ma = 16000;
	in.external_ma = -1;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 0);
	zassert_true(out.blocked);
}

ZTEST(evse_current, test_minimum_of_slots)
{
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 16000);
	in.cable_ma = 13000;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 13000);
	in.input_ma = 10000;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 10000);
	in.external_ma = 8000;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 8000);
	zassert_equal(out.duty, 133);
	zassert_false(out.blocked);
	/* F10: Code 19 ignoriert external */
	in.external_ignored = true;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 10000);
}

ZTEST(evse_current, test_above_limit_accepted)
{
	/* F11: 32000 bei 16-A-Drehschalter wird ueber das Minimum begrenzt */
	in.external_ma = 32000;
	evse_current_eval(&in, &out);
	zassert_equal(out.i_ma, 16000);
	zassert_false(out.implausible);
}

ZTEST(evse_current, test_block_below_6a)
{
	/* SOLL-CUR-11, E19 */
	in.external_ma = 5999;
	evse_current_eval(&in, &out);
	zassert_true(out.blocked);
	zassert_equal(out.duty, EVSE_DUTY_OFF);
	zassert_equal(out.allowed_ma, 0);
	in.external_ma = 0;
	evse_current_eval(&in, &out);
	zassert_true(out.blocked);
	in.external_ma = 6000;
	evse_current_eval(&in, &out);
	zassert_false(out.blocked);
	zassert_equal(out.duty, 100);
}

ZTEST(evse_current, test_pause)
{
	in.pause_input = true;
	evse_current_eval(&in, &out);
	zassert_true(out.blocked);
	in.pause_input = false;
	in.pause_request = true;
	evse_current_eval(&in, &out);
	zassert_true(out.blocked);
}

ZTEST(evse_current, test_pwm5)
{
	/* SOLL-CUR-12: alle drei Quellen, Pause hat Vorrang, Slots wirkungslos */
	in.external_ma = 0;
	in.hardware_pwm5 = true;
	evse_current_eval(&in, &out);
	zassert_true(out.pwm5_mode);
	zassert_equal(out.duty, 50);
	zassert_false(out.blocked);
	in.hardware_pwm5 = false;
	in.input_pwm5 = true;
	evse_current_eval(&in, &out);
	zassert_true(out.pwm5_mode);
	in.input_pwm5 = false;
	in.pwm5_request = true;
	evse_current_eval(&in, &out);
	zassert_true(out.pwm5_mode);
	in.pause_request = true;
	evse_current_eval(&in, &out);
	zassert_false(out.pwm5_mode);
	zassert_true(out.blocked);
}

ZTEST(evse_current, test_write_valid)
{
	zassert_true(evse_current_write_valid(0));
	zassert_false(evse_current_write_valid(1));
	zassert_false(evse_current_write_valid(5999));
	zassert_true(evse_current_write_valid(6000));
	zassert_true(evse_current_write_valid(80000));
	zassert_false(evse_current_write_valid(80001));
	zassert_false(evse_current_write_valid(-1));
}

ZTEST(evse_current, test_rotary)
{
	struct evse_rotary r = evse_rotary_decode(0);

	zassert_true(r.valid && r.pwm5);
	r = evse_rotary_decode(4);
	zassert_true(r.valid);
	zassert_equal(r.limit_ma, 16000);
	r = evse_rotary_decode(9);
	zassert_equal(r.limit_ma, 80000);
	for (uint8_t p = 10; p < 16; p++) {
		r = evse_rotary_decode(p);
		zassert_false(r.valid, "Position %u", p);
	}
}

ZTEST(evse_current, test_dip)
{
	evse_dip_t d = 0x201; /* D1 und D10 */

	zassert_true(evse_dip(d, 1));
	zassert_false(evse_dip(d, 2));
	zassert_true(evse_dip(d, 10));
	zassert_false(evse_dip(d, 11));
}

ZTEST_SUITE(evse_current, NULL, NULL, before, NULL, NULL);
