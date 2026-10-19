/* SPDX-License-Identifier: Apache-2.0 */
/* Stromvorgabe und Tastgrad, SOLL-CUR-01..22 */
#include <wallfree/evse/current.h>

uint16_t evse_duty_from_ma(int32_t ma)
{
	/* SOLL-CUR-20 */
	if (ma < EVSE_CURRENT_MIN_MA) {
		return EVSE_DUTY_OFF;
	}
	if (ma > EVSE_CURRENT_MAX_MA) {
		ma = EVSE_CURRENT_MAX_MA;
	}
	if (ma <= 51000) {
		return (uint16_t)(ma / 60);
	}
	if (ma < 52500) {
		return 850;
	}
	return (uint16_t)(ma / 250 + 640);
}

int32_t evse_ma_from_duty(uint16_t d)
{
	/* HB S. 39 Tab. 6-3, Tastgrad in Promille */
	if (d < 80) {
		return 0; /* < 3 % kein Laden, 3..7 % digital, 7..8 % kein Laden */
	}
	if (d < 100) {
		return 6000;
	}
	if (d <= 850) {
		return (int32_t)d * 60;
	}
	if (d <= 960) {
		return ((int32_t)d - 640) * 250;
	}
	if (d <= 970) {
		return 80000;
	}
	if (d < 1000) {
		return ((int32_t)d - 640) * 250;
	}
	return 0; /* 100 %: keine PWM */
}

bool evse_current_write_valid(int32_t ma)
{
	return ma == 0 || (ma >= EVSE_CURRENT_MIN_MA && ma <= EVSE_CURRENT_MAX_MA);
}

static int32_t min_active(int32_t a, int32_t b)
{
	return b < a ? b : a;
}

void evse_current_eval(const struct evse_current_in *in, struct evse_current_out *out)
{
	bool pause = in->pause_input || in->pause_request;

	out->implausible = false;
	out->pwm5_mode = false;

	/* SOLL-CUR-12: 5-%-Modus; nur eine Pause hat Vorrang */
	if ((in->hardware_pwm5 || in->input_pwm5 || in->pwm5_request) && !pause) {
		out->pwm5_mode = true;
		out->blocked = false;
		out->i_ma = 0;
		out->duty = EVSE_DUTY_PWM5;
		out->allowed_ma = 0;
		return;
	}

	/* SOLL-CUR-10: Minimum aller aktiven Slots */
	int32_t i = in->hardware_ma;

	if (in->cable_ma > 0) {
		i = min_active(i, in->cable_ma);
	}
	if (in->input_ma > 0) {
		i = min_active(i, in->input_ma);
	}
	if (!in->external_ignored) {
		i = min_active(i, in->external_ma);
	}
	if (i < 0) {
		i = 0;
	}
	out->i_ma = i;

	/* SOLL-CUR-13: kann konstruktionsbedingt nicht auftreten */
	if (i > in->hardware_ma || i > EVSE_CURRENT_MAX_MA) {
		out->implausible = true;
	}

	/* SOLL-CUR-11 */
	out->blocked = pause || i < EVSE_CURRENT_MIN_MA || out->implausible;
	if (out->blocked) {
		out->duty = EVSE_DUTY_OFF;
		out->allowed_ma = 0;
		return;
	}
	out->duty = evse_duty_from_ma(i);
	out->allowed_ma = evse_ma_from_duty(out->duty);
}
