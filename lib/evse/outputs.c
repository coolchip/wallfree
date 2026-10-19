/* SPDX-License-Identifier: Apache-2.0 */
/* Ausgangsfunktionen ER/LR/VR/CR, Tabelle 9.3 (Codes 0..42) */
#include <wallfree/evse/charger.h>

static bool cable_valid(int16_t a)
{
	return a == 13 || a == 20 || a == 32 || a == 63;
}

bool evse_dout_eval(const struct evse *e, uint8_t code, evse_ms_t now)
{
	enum evse_cp_state st = e->status;
	bool err = e->errors != 0;
	bool pwm = e->cp_duty >= 30 && e->cp_duty <= 970;
	bool a = st == EVSE_CP_A, b = st == EVSE_CP_B, c = st == EVSE_CP_C;
	bool d = st == EVSE_CP_D, ee = st == EVSE_CP_E, f = st == EVSE_CP_F;
	bool ab_ok = (a || b) && !err;
	int16_t cable = e->pp.cable_a;

	switch (code) {
	case 0:
		return false;
	case 1:
		return a && !err;
	case 2:
		return b && !err;
	case 3:
		return b && pwm && !err;
	case 4:
		return b && !pwm && !err;
	case 5:
		return c;
	case 6:
		return d;
	case 7:
		return ee || (err && !f);
	case 8:
		return f;
	case 9:
		return ab_ok;
	case 10:
		return (a || (b && pwm)) && !err;
	case 11:
		return (a || (b && !pwm)) && !err;
	case 12:
		return ab_ok || c;
	case 13:
		return ab_ok || d;
	case 14:
		return ab_ok || c || d;
	case 15:
		return ee || f || err;
	case 16:
		return c || d;
	case 17:
		return pwm;
	case 18:
		return cable_valid(cable);
	case 19:
		return !cable_valid(cable);
	case 20:
		return cable == 13;
	case 21:
		return cable == 20;
	case 22:
		return cable == 32;
	case 23:
		return cable == 63;
	case 24:
		return cable == 13 || cable == 20;
	case 25:
		return cable == 13 || cable == 20 || cable == 32;
	case 26:
		return (e->errors & EVSE_ERR_CABLE_REJECT) != 0;
	case 27:
		return e->contactor;
	case 28:
		return e->state == EVSE_S6_CHARGING && d; /* Belueftung */
	case 29:
		return e->last_lock_wish;
	case 30:
	case 31:
	case 32:
	case 33:
		return e->rt.register_output[code - 30];
	case 34:
		return (e->errors & EVSE_ERR_OVERCURRENT) != 0;
	case 35:
		return (e->errors & EVSE_ERR_CONTACTOR) != 0;
	case 36:
		return (e->errors & EVSE_ERR_STATE_D) != 0;
	case 37:
		return (b && !err) || c || d;
	case 38:
		/* Code 19 aktiv bei A/B, Nachlauf 1 s nach Schuetz aus */
		return e->din.max_current &&
		       ((a || b) || (!e->contactor && (now - e->contactor_off_since) < 1000));
	case 39:
		return e->release && (c || d || (b && e->was_blocked));
	case 40:
		if (e->state == EVSE_S0_INIT) {
			return (now % 1000) < 500;
		}
		return ab_ok;
	case 41:
		return ab_ok && !pwm;
	case 42:
		/* "Suspended EV": Fahrzeug pausiert nach Laden bei laufender PWM */
		return e->state == EVSE_S5_PWM_ON && b && pwm && e->charged_this_plug;
	default:
		return false;
	}
}
