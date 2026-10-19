/* SPDX-License-Identifier: Apache-2.0 */
/* Digitaleingaenge, SOLL-IO-10..12, Abschnitt 9.1 */
#include <wallfree/evse/din.h>

bool evse_in_code_valid(int code)
{
	return code >= 0 && code <= EVSE_IN_CODE_MAX;
}

int32_t evse_in_code_limit_ma(int code)
{
	static const int32_t ma[] = {6000, 10000, 13000, 16000, 20000, 32000, 63000, 70000};

	if (code >= EVSE_IN_MAX_6A && code <= EVSE_IN_MAX_70A) {
		return ma[code - EVSE_IN_MAX_6A];
	}
	return 0;
}

void evse_din_init(struct evse_din *din)
{
	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		din->ch[i].hyst = false;
		din->ch[i].stable = false;
		din->ch[i].agree = 0;
	}
	din->en_toggle = false;
	din->en_toggle_last = EVSE_MS_NONE;
	din->ml_toggle_last = EVSE_MS_NONE;
	din->en = din->xr = din->ld = false;
	din->ct_no = din->ct_nc = din->pwm5 = false;
	din->limit_ma = 0;
	din->max_current = din->pause = din->error_input = false;
	din->ml_event = EVSE_ML_EV_NONE;
}

static bool toggle_allowed(evse_ms_t *last, evse_ms_t now)
{
	if (*last != EVSE_MS_NONE && (now - *last) < EVSE_DIN_TOGGLE_MS) {
		return false;
	}
	*last = now;
	return true;
}

void evse_din_update(struct evse_din *din, const struct evse_din_config *cfg,
		     const int32_t mv[EVSE_DIN_COUNT], evse_ms_t now)
{
	bool rising[EVSE_DIN_COUNT];
	bool falling[EVSE_DIN_COUNT];

	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		struct evse_din_channel *c = &din->ch[i];

		/* SOLL-IO-10: Hysterese 3 V / 9 V */
		if (mv[i] > EVSE_DIN_ON_MV) {
			c->hyst = true;
		} else if (mv[i] < EVSE_DIN_OFF_MV) {
			c->hyst = false;
		}
		/* SOLL-IO-12: Wechsel erst nach EVSE_DIN_AGREE gleichen Bloecken */
		rising[i] = falling[i] = false;
		if (c->hyst != c->stable) {
			if (++c->agree >= EVSE_DIN_AGREE) {
				c->stable = c->hyst;
				c->agree = 0;
				rising[i] = c->stable;
				falling[i] = !c->stable;
			}
		} else {
			c->agree = 0;
		}
	}

	bool en_level = false;
	bool en_pulsed_cfg = false;
	bool xr = false, ld = false, ct_no = false, ct_nc = false, pwm5 = false;
	bool maxc = false, pause = false, err = false;
	int32_t limit = 0;
	enum evse_ml_event ml = EVSE_ML_EV_NONE;

	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		bool on = din->ch[i].stable;

		switch (cfg->code[i]) {
		case EVSE_IN_EN:
		case EVSE_IN_EN_PORTAL: /* F18: wie Code 1 */
			en_level |= on;
			break;
		case EVSE_IN_EN_PULSED:
			en_pulsed_cfg = true;
			if (rising[i] && toggle_allowed(&din->en_toggle_last, now)) {
				din->en_toggle = !din->en_toggle;
			}
			break;
		case EVSE_IN_XR:
			xr |= on;
			break;
		case EVSE_IN_LD:
			ld |= on;
			break;
		case EVSE_IN_ML:
			if (rising[i]) {
				ml = EVSE_ML_EV_SET1;
			} else if (falling[i] && ml == EVSE_ML_EV_NONE) {
				ml = EVSE_ML_EV_SET0;
			}
			break;
		case EVSE_IN_ML_PULSED:
			if (rising[i] && toggle_allowed(&din->ml_toggle_last, now)) {
				ml = EVSE_ML_EV_TOGGLE;
			}
			break;
		case EVSE_IN_CT_NO:
			ct_no |= on;
			break;
		case EVSE_IN_CT_NC:
			ct_nc |= on;
			break;
		case EVSE_IN_PWM5:
			pwm5 |= on;
			break;
		case EVSE_IN_MAX_CURRENT:
			maxc |= on;
			break;
		case EVSE_IN_PAUSE:
			pause |= on;
			break;
		case EVSE_IN_ERROR:
			err |= on;
			break;
		default: {
			int32_t l = evse_in_code_limit_ma(cfg->code[i]);

			if (l > 0 && on && (limit == 0 || l < limit)) {
				limit = l;
			}
			break;
		}
		}
	}

	if (!en_pulsed_cfg) {
		din->en_toggle = false;
	}
	din->en = en_level || din->en_toggle;
	din->xr = xr;
	din->ld = ld;
	din->ct_no = ct_no;
	din->ct_nc = ct_nc;
	din->pwm5 = pwm5;
	din->limit_ma = limit;
	din->max_current = maxc;
	din->pause = pause;
	din->error_input = err;
	din->ml_event = ml;
}

enum evse_ld_mode evse_din_ld_source(const struct evse_din_config *cfg, int *ch_out)
{
	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		if (cfg->code[i] == EVSE_IN_LD_PULSED) {
			*ch_out = i;
			return EVSE_LD_PULSED;
		}
	}
	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		if (cfg->code[i] == EVSE_IN_LD) {
			*ch_out = i;
			return EVSE_LD_PERMANENT;
		}
	}
	*ch_out = -1;
	return EVSE_LD_NONE;
}
