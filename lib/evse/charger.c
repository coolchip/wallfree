/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Ladeautomat S0..S10, Soll-Spezifikation Abschnitte 5, 7, 9, 11.
 * Kennungen SOLL-* verweisen auf docs/soll-spezifikation.md.
 */
#include <string.h>
#include <wallfree/evse/charger.h>

/* Fehler, die bei jedem Zyklus aus den Modulen neu bestimmt werden */
#define ERR_DYNAMIC (EVSE_ERR_CABLE_REJECT | EVSE_ERR_PP_INVALID | EVSE_ERR_CP_INVALID | \
		     EVSE_ERR_DIODE | EVSE_ERR_INPUT | EVSE_ERR_CONTACTOR | EVSE_ERR_RCM_ANY)

void evse_config_defaults(struct evse_config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->rotary = 4;
	/* E20: LD 3, EN 1, ML 4, XR 2, IN 11; ER 15, LR 29, VR 16, CR 17 */
	cfg->din.code[EVSE_DIN_LD] = EVSE_IN_LD;
	cfg->din.code[EVSE_DIN_EN] = EVSE_IN_EN;
	cfg->din.code[EVSE_DIN_ML] = EVSE_IN_ML;
	cfg->din.code[EVSE_DIN_XR] = EVSE_IN_XR;
	cfg->din.code[EVSE_DIN_IN] = EVSE_IN_MAX_16A;
	cfg->out_function[EVSE_DOUT_ER] = 15;
	cfg->out_function[EVSE_DOUT_LR] = 29;
	cfg->out_function[EVSE_DOUT_VR] = 16;
	cfg->out_function[EVSE_DOUT_CR] = 17;
	cfg->lock_pulse_ms = 500;
	cfg->unlock_pulse_ms = 500;
	cfg->lock_cycle_gap_ms = 2000;
	cfg->contactor_min_off_ms = 500;
	cfg->contactor_monitor = EVSE_CTM_SENSE;
	cfg->reject_state_d = true;
	cfg->wakeup_delay_ms = 30000;
	cfg->wakeup_mode = 0;
}

static bool dip(const struct evse *e, int n)
{
	return evse_dip(e->cfg.dip, n);
}

static bool cp_is_bcd(enum evse_cp_state s)
{
	return s == EVSE_CP_B || s == EVSE_CP_C || s == EVSE_CP_D;
}

static bool duty_is_pwm(uint16_t d)
{
	return d >= 30 && d <= 970;
}

void evse_init(struct evse *e, const struct evse_config *cfg, enum evse_lock_state stored_lock,
	       evse_ms_t now)
{
	int ch;

	memset(e, 0, sizeof(*e));
	e->cfg = *cfg;

	e->rt.external_ma = EVSE_CURRENT_MAX_MA; /* F12: Default keine Einschraenkung */
	e->rt.enable_register = false;           /* F19: Default 0 */
	e->rt.available_register = true;         /* SOLL-EN-11 */

	evse_cp_init(&e->cp);
	evse_pp_init(&e->pp);
	evse_din_init(&e->din);
	evse_lock_init(&e->lock, stored_lock);
	evse_rcm_init(&e->rcm);
	evse_ctm_init(&e->ctm);

	e->rotary = evse_rotary_decode(cfg->rotary);
	e->ld_mode = evse_din_ld_source(&cfg->din, &ch);
	e->lock_cfg = (struct evse_lock_config){
		.d4 = evse_dip(cfg->dip, 4),
		.d6 = evse_dip(cfg->dip, 6),
		.ld_mode = e->ld_mode,
		.lock_pulse_ms = cfg->lock_pulse_ms,
		.unlock_pulse_ms = cfg->unlock_pulse_ms,
		.cycle_gap_ms = cfg->lock_cycle_gap_ms,
		.unlock_check = cfg->unlock_check,
	};

	/* SOLL-BOOT-06: Konfigurationspruefung */
	if (!e->rotary.valid) {
		e->errors |= EVSE_ERR_SWITCH; /* E11 */
	}
	if (e->lock_cfg.d4 && e->lock_cfg.d6 && e->ld_mode == EVSE_LD_NONE) {
		e->errors |= EVSE_ERR_CONFIG; /* F17 */
	}
	if (!evse_ctm_mode_valid(cfg->contactor_monitor, cfg->meter_available)) {
		e->errors |= EVSE_ERR_CONFIG;
	}

	e->state = EVSE_S0_INIT;
	e->state_since = now;
	e->start_ms = now;
	e->contactor = false;
	e->contactor_off_since = now;
	e->cp_duty = EVSE_DUTY_OFF;
	e->pwm_since = EVSE_MS_NONE;
	e->diode_since = EVSE_MS_NONE;
	e->stop_since = EVSE_MS_NONE;
	e->b_since = EVSE_MS_NONE;
	e->wake_since = EVSE_MS_NONE;
	e->ld_lost_since = EVSE_MS_NONE;
	e->supply_low_since = EVSE_MS_NONE;
	e->supply_good_since = EVSE_MS_NONE;
	e->rcm_retest_last = EVSE_MS_NONE;
	e->oc125_since = EVSE_MS_NONE;
	e->oc110_since = EVSE_MS_NONE;
	e->charge_start = EVSE_MS_NONE;
	evse_debounce_init(&e->status_deb, EVSE_CP_A);
	e->status = EVSE_CP_A;
}

/* ---------------------------------------------------------------- Hilfen */

static void contactor_off(struct evse *e, evse_ms_t now)
{
	if (e->contactor) {
		e->contactor = false;
		e->contactor_off_since = now;
	}
}

static bool lock_req(struct evse *e, bool lock, evse_ms_t now)
{
	if (!evse_lock_request(&e->lock, &e->lock_cfg, lock, now)) {
		return false;
	}
	e->last_lock_wish = lock;
	return true;
}

static void plug_reset(struct evse *e)
{
	e->selftest_passed = false;
	e->wake_done = false;
	e->wake_phase = 0;
	e->lock_attempts = 0;
	e->unlock_attempts = 0;
	e->lock_fail_unlocking = false;
	e->unlock_fail_relocking = false;
	e->relock_mode = false;
	e->charged_this_plug = false;
	e->was_blocked = false;
	e->charge_ms = 0;
}

static void goto_state(struct evse *e, enum evse_state s, evse_ms_t now)
{
	if (e->state == s) {
		return;
	}
	if (e->state == EVSE_S4_SELFTEST) {
		evse_rcm_selftest_abort(&e->rcm); /* SOLL-SM-42 */
	}
	if (e->state == EVSE_S6_CHARGING && e->charge_start != EVSE_MS_NONE) {
		e->charge_ms += now - e->charge_start;
		e->charge_start = EVSE_MS_NONE;
	}
	e->state = s;
	e->state_since = now;
	e->wake_phase = 0;
	e->digital_comm_unavailable = false;

	switch (s) {
	case EVSE_S4_SELFTEST:
		/* Schuetz ist in allen Vorgaengerzustaenden aus */
		(void)evse_rcm_selftest_start(&e->rcm, &e->rcm_lv, now);
		break;
	case EVSE_S5_PWM_ON:
		e->b_since = EVSE_MS_NONE;
		break;
	case EVSE_S6_CHARGING:
		e->charge_start = now;
		e->charged_this_plug = true;
		e->was_blocked = false;
		break;
	case EVSE_S7_STOPPING:
		e->stop_since = now;
		break;
	default:
		break;
	}
}

static int target_rank(enum evse_state s)
{
	switch (s) {
	case EVSE_S10_UNAVAILABLE:
		return 4;
	case EVSE_S9_ERROR:
		return 3;
	case EVSE_S8_UNLOCKING:
		return 2;
	default:
		return 1;
	}
}

/* Geordnetes Ladeende (S7) mit Ziel; ein hoeherrangiges Ziel ersetzt ein niedrigeres */
static void stop(struct evse *e, enum evse_state target, evse_ms_t now)
{
	if (e->state == EVSE_S7_STOPPING) {
		if (target_rank(target) > target_rank(e->stop_target)) {
			e->stop_target = target;
		}
		return;
	}
	e->stop_target = target;
	goto_state(e, EVSE_S7_STOPPING, now);
}

static bool locked(const struct evse *e)
{
	return e->lock.state == EVSE_LOCK_LOCKED;
}

/* Zustand nach Abstecken waehrend PWM/Laden */
static void after_unplug(struct evse *e, evse_ms_t now)
{
	if (e->lock_cfg.d4 && !dip(e, 9) && (locked(e) || evse_lock_busy(&e->lock))) {
		goto_state(e, EVSE_S8_UNLOCKING, now);
	} else {
		goto_state(e, EVSE_S1_IDLE, now);
	}
}

/* --------------------------------------------------------- Messung/Schutz */

static void measure(struct evse *e, const struct evse_inputs *in, evse_ms_t now)
{
	struct evse_cp_sample s = {
		.pos_mv = in->cp_pos_mv,
		.neg_mv = in->cp_neg_mv,
		.pos_valid = e->cp_duty > 0,
		.neg_valid = e->cp_duty < EVSE_DUTY_OFF,
		.pwm_active = duty_is_pwm(e->cp_duty),
		.pwm_since = e->pwm_since,
		.current_blocked = e->cur.blocked,
	};
	struct evse_pp_config pc = {
		.d1_proximity = dip(e, 1),
		.d2_reject = dip(e, 2),
		.d3_only13 = dip(e, 3),
		.rotary_a = (int16_t)(e->rotary.limit_ma / 1000),
	};

	evse_cp_update(&e->cp, &s, now);
	evse_pp_update(&e->pp, &pc, in->pp_mv, evse_cp_raw(&e->cp), now);
	if (e->pp.unplugged_event) {
		e->errors &= ~EVSE_ERR_LOCK; /* SOLL-PP-06 */
	}
	evse_din_update(&e->din, &e->cfg.din, in->din_mv, now);

	switch (e->din.ml_event) {
	case EVSE_ML_EV_SET0:
		e->rt.lock_request = false;
		break;
	case EVSE_ML_EV_SET1:
		e->rt.lock_request = true;
		break;
	case EVSE_ML_EV_TOGGLE:
		e->rt.lock_request = !e->rt.lock_request;
		break;
	default:
		break;
	}
}

static void protect(struct evse *e, const struct evse_inputs *in, evse_ms_t now)
{
	/* SOLL-PROT-04: Fehlerstrom in jedem Zustand */
	e->rcm_lv = in->rcm;
	if (e->rcm.test == EVSE_RCM_TEST_RUNNING) {
		evse_rcm_selftest_update(&e->rcm, &in->rcm, now);
	} else if (evse_rcm_monitor(&e->rcm, &in->rcm)) {
		contactor_off(e, now);
	}

	/* SOLL-PROT-20..23 */
	struct evse_ctm_in ci = {
		.contactor_on = e->contactor,
		.off_since = e->contactor_off_since,
		.last_sense_edge = in->last_sense_edge,
		.ct_no = e->din.ct_no,
		.ct_nc = e->din.ct_nc,
		.meter_valid = in->meter_valid,
		.phase_v = {in->phase_v[0], in->phase_v[1], in->phase_v[2]},
	};
	evse_ctm_update(&e->ctm, (enum evse_ctm_mode)e->cfg.contactor_monitor, &ci, now);

	/* SOLL-PROT-41 (F16): nur Warnung */
	if (in->supply_mv < 9000) {
		e->supply_good_since = EVSE_MS_NONE;
		if (e->supply_low_since == EVSE_MS_NONE) {
			e->supply_low_since = now;
		}
		if (evse_elapsed_gt(now, e->supply_low_since, 100)) {
			e->warnings |= EVSE_WARN_SUPPLY;
		}
	} else if (in->supply_mv > 10000) {
		e->supply_low_since = EVSE_MS_NONE;
		if (e->supply_good_since == EVSE_MS_NONE) {
			e->supply_good_since = now;
		}
		if (evse_elapsed_ge(now, e->supply_good_since, 2500)) {
			e->warnings &= ~EVSE_WARN_SUPPLY;
		}
	}

	/* SOLL-PROT-30/31: Ueberstrom nur mit Messquelle */
	if (e->cfg.overcurrent_enable && e->cfg.meter_available && e->contactor) {
		if (!in->meter_valid) {
			e->errors |= EVSE_ERR_METER_COMM;
		} else {
			int32_t imax = in->phase_ma[0];

			for (int i = 1; i < 3; i++) {
				if (in->phase_ma[i] > imax) {
					imax = in->phase_ma[i];
				}
			}
			int64_t ref = e->cur.allowed_ma;

			if ((int64_t)imax * 100 >= ref * 125) {
				if (e->oc125_since == EVSE_MS_NONE) {
					e->oc125_since = now;
				}
			} else {
				e->oc125_since = EVSE_MS_NONE;
			}
			if ((int64_t)imax * 100 >= ref * 110) {
				if (e->oc110_since == EVSE_MS_NONE) {
					e->oc110_since = now;
				}
			} else {
				e->oc110_since = EVSE_MS_NONE;
			}
			if (evse_elapsed_ge(now, e->oc125_since, 5000) ||
			    evse_elapsed_ge(now, e->oc110_since, 95000)) {
				e->errors |= EVSE_ERR_OVERCURRENT;
			}
		}
	} else {
		e->oc125_since = e->oc110_since = EVSE_MS_NONE;
	}
}

static void update_errors(struct evse *e, evse_ms_t now)
{
	evse_err_t dyn = 0;

	if (e->pp.reject) {
		dyn |= EVSE_ERR_CABLE_REJECT;
	}
	if (e->pp.invalid) {
		dyn |= EVSE_ERR_PP_INVALID;
	}
	if (e->cp.invalid) {
		dyn |= EVSE_ERR_CP_INVALID;
	}
	if (e->cp.diode_fault) {
		dyn |= EVSE_ERR_DIODE;
	}
	if (e->din.error_input) {
		dyn |= EVSE_ERR_INPUT;
	}
	if (e->ctm.fault) {
		dyn |= EVSE_ERR_CONTACTOR;
	}
	if (e->rcm.faults & EVSE_RCM_F_SENSOR) {
		dyn |= EVSE_ERR_RCM_SENSOR;
	}
	if (e->rcm.faults & EVSE_RCM_F_DC6) {
		dyn |= EVSE_ERR_RCM_DC6;
	}
	if (e->rcm.faults & EVSE_RCM_F_AC30) {
		dyn |= EVSE_ERR_RCM_AC30;
	}
	if (e->rcm.faults & EVSE_RCM_F_SELFTEST) {
		dyn |= EVSE_ERR_RCM_SELFTEST;
	}
	if ((dyn & EVSE_ERR_DIODE) && !(e->errors & EVSE_ERR_DIODE)) {
		e->diode_since = now;
	}
	e->errors = (e->errors & ~ERR_DYNAMIC) | dyn;
	e->errors_seen |= e->errors;

	/* SOLL-PROT-01: Schutzfehler sperren das Schuetz */
	if (e->errors & EVSE_ERR_CLASS_P) {
		contactor_off(e, now);
	}
}

static void update_status(struct evse *e, evse_ms_t now)
{
	enum evse_cp_state raw = evse_cp_raw(&e->cp);
	evse_err_t err = e->errors;

	/* SOLL-CP-08 */
	if (e->avail_missing || (err & (EVSE_ERR_DIODE | EVSE_ERR_CONTACTOR))) {
		e->status = EVSE_CP_F;
		evse_debounce_init(&e->status_deb, EVSE_CP_F);
		return;
	}
	if (err & EVSE_ERR_CP_INVALID) {
		e->status = EVSE_CP_E;
		evse_debounce_init(&e->status_deb, EVSE_CP_E);
		return;
	}

	enum evse_cp_state forced = raw;
	evse_err_t always = err & ~(EVSE_ERR_E_NOT_A);

	if (always) {
		forced = EVSE_CP_E;
	} else if (raw != EVSE_CP_A) {
		evse_err_t not_a = err & EVSE_ERR_E_NOT_A & ~EVSE_ERR_LOCK;

		if (not_a) {
			forced = EVSE_CP_E;
		}
		if ((err & EVSE_ERR_LOCK) &&
		    (e->lock_attempts >= EVSE_LOCK_E_FROM || dip(e, 9))) {
			forced = EVSE_CP_E;
		}
	}
	(void)evse_debounce_update(&e->status_deb, forced, now, EVSE_CP_DEBOUNCE_MS);
	e->status = (enum evse_cp_state)e->status_deb.value;
}

static void update_release_and_current(struct evse *e)
{
	struct evse_current_in ci;

	/* SOLL-EN-10 */
	e->avail_missing = (dip(e, 8) && !e->din.xr) || (dip(e, 10) && !e->rt.available_register);
	/* SOLL-EN-01 */
	e->permission = (dip(e, 7) && e->din.en) || (dip(e, 10) && e->rt.enable_register) ||
			(!dip(e, 7) && !dip(e, 10));
	/* SOLL-EN-02 */
	e->release = e->permission && (!e->lock_cfg.d4 || locked(e)) && e->errors == 0;

	int32_t hw = e->rotary.valid ? e->rotary.limit_ma : 0;

	if (e->rt.hardware_limit_ma > 0 && e->rt.hardware_limit_ma < hw) {
		hw = e->rt.hardware_limit_ma; /* SOLL-PAR-40: nur absenken */
	}
	ci = (struct evse_current_in){
		.hardware_ma = hw,
		.hardware_pwm5 = e->rotary.valid && e->rotary.pwm5,
		.cable_ma = (dip(e, 1) && e->pp.cable_a > 0) ? (int32_t)e->pp.cable_a * 1000 : 0,
		.input_ma = e->din.limit_ma,
		.input_pwm5 = e->din.pwm5,
		.external_ma = e->rt.external_ma,
		.external_ignored = e->din.max_current,
		.pwm5_request = e->rt.pwm5_request,
		.pause_input = e->din.pause,
		.pause_request = e->rt.pause_request,
	};
	evse_current_eval(&ci, &e->cur);
	if (e->cur.implausible) {
		e->errors |= EVSE_ERR_INTERNAL; /* SOLL-CUR-13 */
	}
}

static void rcm_retest(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	/* SOLL-PROT-11 (3), F13 */
	if (!(e->errors & EVSE_ERR_RCM_ANY) || raw != EVSE_CP_A || e->contactor ||
	    e->rcm.test == EVSE_RCM_TEST_RUNNING) {
		return;
	}
	if (e->rcm_retest_last != EVSE_MS_NONE &&
	    !evse_elapsed_ge(now, e->rcm_retest_last, EVSE_RCM_RETEST_MS)) {
		return;
	}
	e->rcm_retest_last = now;
	(void)evse_rcm_selftest_start(&e->rcm, &e->rcm_lv, now);
}

static void clear_at_a(struct evse *e)
{
	e->errors &= ~EVSE_ERR_RESET_AT_A;
	evse_cp_clear_diode(&e->cp);
	e->rt.unlock_request = false;
	plug_reset(e);
}

/* ----------------------------------------------------- Vorrangliste 5.2 */

static bool unlock_wish(const struct evse *e)
{
	return e->rt.unlock_request || (dip(e, 9) && !e->rt.lock_request && locked(e));
}

static bool prelude(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	evse_err_t err = e->errors;
	enum evse_state s = e->state;
	bool pwm_states = s == EVSE_S5_PWM_ON || s == EVSE_S6_CHARGING || s == EVSE_S7_STOPPING;

	/* Rang 1/2 */
	if (err & (EVSE_ERR_CLASS_P | EVSE_ERR_CLASS_K)) {
		contactor_off(e, now);
		goto_state(e, (err & EVSE_ERR_CONTACTOR) ? EVSE_S10_UNAVAILABLE : EVSE_S9_ERROR, now);
		return true;
	}
	/* Rang 3 */
	if (raw == EVSE_CP_E || (err & (EVSE_ERR_CP_INVALID | EVSE_ERR_DIODE))) {
		contactor_off(e, now);
		goto_state(e, EVSE_S9_ERROR, now);
		return true;
	}
	/* Rang 4 */
	if (raw == EVSE_CP_A && pwm_states) {
		contactor_off(e, now);
		after_unplug(e, now);
		return true;
	}
	/* Rang 5 */
	if (err & EVSE_ERR_CLASS_VI) {
		contactor_off(e, now);
		goto_state(e, EVSE_S9_ERROR, now);
		return true;
	}
	/* Rang 6/7 */
	if (s != EVSE_S8_UNLOCKING && (e->avail_missing || (err & EVSE_ERR_CLASS_VO))) {
		enum evse_state t = e->avail_missing ? EVSE_S10_UNAVAILABLE : EVSE_S9_ERROR;

		if (pwm_states) {
			stop(e, t, now);
			/* S7 schreitet selbst fort; nur das Ziel wurde angehoben */
			return s != EVSE_S7_STOPPING;
		}
		goto_state(e, t, now);
		return true;
	}
	if (s == EVSE_S7_STOPPING) {
		if (unlock_wish(e)) {
			stop(e, EVSE_S8_UNLOCKING, now);
		}
		return false; /* S7 schreitet selbst fort */
	}
	/* Rang 8 */
	bool startable = s == EVSE_S4_SELFTEST || s == EVSE_S5_PWM_ON || s == EVSE_S6_CHARGING;

	if (startable && (!e->release || e->cur.blocked)) {
		e->was_blocked = true;
		if (e->cfg.wakeup_mode == 1) {
			e->wake_done = false; /* E17: wallbe-Verhalten */
		}
		if (s == EVSE_S6_CHARGING) {
			stop(e, EVSE_S3_WAIT, now);
		} else {
			goto_state(e, EVSE_S3_WAIT, now);
		}
		return true;
	}
	/* Rang 9 */
	if (unlock_wish(e) && (s == EVSE_S3_WAIT || startable)) {
		if (s == EVSE_S6_CHARGING) {
			stop(e, EVSE_S8_UNLOCKING, now);
		} else {
			goto_state(e, EVSE_S8_UNLOCKING, now);
		}
		return true;
	}
	return false;
}

/* --------------------------------------------------------- Zustaende 5.3 */

static void st_init(struct evse *e, enum evse_lock_result res, evse_ms_t now)
{
	if (!e->init_selftest_started) {
		e->init_selftest_started = true;
		(void)evse_rcm_selftest_start(&e->rcm, &e->rcm_lv, now);
	}
	if (!e->init_unlock_started) {
		e->init_unlock_started = true;
		(void)lock_req(e, false, now); /* SOLL-LOCK-20 */
	}
	if (res == EVSE_LOCK_RES_FAIL) {
		e->errors |= EVSE_ERR_UNLOCK;
	}
	if (evse_elapsed_ge(now, e->start_ms, EVSE_STARTUP_MS) &&
	    e->rcm.test != EVSE_RCM_TEST_RUNNING && !evse_lock_busy(&e->lock)) {
		goto_state(e, EVSE_S1_IDLE, now); /* SOLL-SM-01 */
	}
}

static void st_idle(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	if (raw == EVSE_CP_A) {
		clear_at_a(e); /* SOLL-SM-10 */
	}
	rcm_retest(e, raw, now);
	if (e->rcm.test == EVSE_RCM_TEST_RUNNING) {
		return;
	}
	if (e->errors & (EVSE_ERR_CLASS_P | EVSE_ERR_CLASS_K)) {
		goto_state(e, (e->errors & EVSE_ERR_CONTACTOR) ? EVSE_S10_UNAVAILABLE : EVSE_S9_ERROR,
			   now);
		return;
	}
	if (raw == EVSE_CP_E || (e->errors & EVSE_ERR_CP_INVALID)) {
		goto_state(e, EVSE_S9_ERROR, now);
		return;
	}
	if (e->avail_missing) {
		goto_state(e, EVSE_S10_UNAVAILABLE, now);
		return;
	}
	if (raw == EVSE_CP_A && e->lock_cfg.d4 && !dip(e, 9) && locked(e)) {
		goto_state(e, EVSE_S8_UNLOCKING, now); /* SOLL-SM-11 */
		return;
	}
	if ((dip(e, 9) && e->rt.lock_request) || cp_is_bcd(raw)) {
		goto_state(e, EVSE_S2_LOCKING, now); /* SOLL-SM-12/13 */
	}
}

static void st_locking(struct evse *e, enum evse_cp_state raw, enum evse_lock_result res,
		       evse_ms_t now)
{
	bool busy = evse_lock_busy(&e->lock);

	/* SOLL-LOCK-40: Wiederverriegeln nach LD-Verlust */
	if (e->relock_mode) {
		if (res == EVSE_LOCK_RES_OK) {
			e->errors &= ~EVSE_ERR_LD_LOST;
			e->relock_mode = false;
			goto_state(e, EVSE_S3_WAIT, now);
		} else if (res == EVSE_LOCK_RES_FAIL) {
			e->relock_mode = false;
			goto_state(e, EVSE_S9_ERROR, now);
		} else if (!busy && !e->relock_requested) {
			e->relock_requested = lock_req(e, true, now);
		}
		return;
	}

	/* Ergebnisse (SOLL-LOCK-21) */
	if (res != EVSE_LOCK_RES_NONE) {
		if (e->lock_fail_unlocking) {
			e->lock_fail_unlocking = false;
		} else if (res == EVSE_LOCK_RES_OK) {
			if (e->last_lock_wish) {
				e->errors &= ~EVSE_ERR_LOCK;
			}
		} else if (e->last_lock_wish) {
			e->errors |= EVSE_ERR_LOCK;
			if (++e->lock_attempts >= EVSE_LOCK_ATTEMPTS) {
				goto_state(e, EVSE_S9_ERROR, now);
				return;
			}
			e->lock_fail_unlocking = lock_req(e, false, now);
			return;
		}
	}
	if (busy) {
		return;
	}
	/* SOLL-SM-21 */
	if (e->rt.unlock_request) {
		if (raw == EVSE_CP_A) {
			after_unplug(e, now);
		}
		return;
	}
	/* SOLL-SM-22 */
	if (dip(e, 9) && !e->rt.lock_request) {
		goto_state(e, locked(e) ? EVSE_S8_UNLOCKING : EVSE_S1_IDLE, now);
		return;
	}
	/* SOLL-SM-23 */
	if (raw == EVSE_CP_A && !dip(e, 9)) {
		goto_state(e, locked(e) ? EVSE_S8_UNLOCKING : EVSE_S1_IDLE, now);
		return;
	}
	/* SOLL-SM-20 */
	if (dip(e, 1) && e->pp.cable_a == 0 && !locked(e)) {
		return;
	}
	if (locked(e)) {
		if (cp_is_bcd(raw)) {
			goto_state(e, EVSE_S3_WAIT, now); /* SOLL-SM-24 */
		}
		return;
	}
	(void)lock_req(e, true, now);
}

static void st_wait(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	if (raw == EVSE_CP_A) {
		/* SOLL-SM-31 */
		if (!dip(e, 9) && e->lock_cfg.d4 && locked(e)) {
			goto_state(e, EVSE_S8_UNLOCKING, now);
		} else {
			goto_state(e, EVSE_S1_IDLE, now);
		}
		return;
	}
	/* SOLL-SM-30 */
	if (e->release && !e->cur.blocked && !e->avail_missing && cp_is_bcd(raw)) {
		goto_state(e, e->selftest_passed ? EVSE_S5_PWM_ON : EVSE_S4_SELFTEST, now);
	}
}

static void st_selftest(struct evse *e, evse_ms_t now)
{
	if (e->rcm.test == EVSE_RCM_TEST_PASSED) {
		e->selftest_passed = true; /* SOLL-SM-40 */
		goto_state(e, EVSE_S5_PWM_ON, now);
	} else if (e->rcm.test != EVSE_RCM_TEST_RUNNING) {
		goto_state(e, EVSE_S9_ERROR, now); /* SOLL-SM-41 */
	}
}

static void st_pwm_on(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	/* Weckfolge 5.4 */
	if (e->wake_phase == 1) {
		if (evse_elapsed_ge(now, e->wake_since, EVSE_WAKE_NEG_MS)) {
			e->wake_phase = 2;
			e->wake_since = now;
		}
		return;
	}
	if (e->wake_phase == 2) {
		if (evse_elapsed_ge(now, e->wake_since, EVSE_WAKE_POS_MS)) {
			e->wake_phase = 0;
			e->wake_done = true;
			e->b_since = EVSE_MS_NONE;
		}
		return;
	}

	bool c_ok = raw == EVSE_CP_C || (raw == EVSE_CP_D && !e->cfg.reject_state_d);

	/* SOLL-SM-53 */
	if (e->cur.pwm5_mode) {
		e->digital_comm_unavailable = raw == EVSE_CP_C || raw == EVSE_CP_D;
		return;
	}
	/* SOLL-SM-52 */
	if (raw == EVSE_CP_D && e->cfg.reject_state_d) {
		e->errors |= EVSE_ERR_STATE_D;
		return;
	}
	/* SOLL-SM-51/60 */
	if (c_ok) {
		e->b_since = EVSE_MS_NONE;
		if (duty_is_pwm(e->cp_duty) &&
		    evse_elapsed_ge(now, e->pwm_since, EVSE_PWM_SETTLE_MS) &&
		    evse_elapsed_ge(now, e->contactor_off_since, e->cfg.contactor_min_off_ms) &&
		    e->release && e->errors == 0 && !(e->errors & EVSE_ERR_CLASS_P)) {
			e->contactor = true;
			goto_state(e, EVSE_S6_CHARGING, now);
		}
		return;
	}
	/* SOLL-SM-54/55 */
	if (raw == EVSE_CP_B) {
		if (e->b_since == EVSE_MS_NONE) {
			e->b_since = now;
		}
		if (e->cfg.wakeup_delay_ms > 0 && !e->wake_done &&
		    evse_elapsed_ge(now, e->b_since, e->cfg.wakeup_delay_ms)) {
			e->wake_phase = 1;
			e->wake_since = now;
		}
	} else {
		e->b_since = EVSE_MS_NONE;
	}
}

static void st_charging(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	if (raw == EVSE_CP_B) {
		contactor_off(e, now); /* SOLL-SM-62 */
		goto_state(e, EVSE_S5_PWM_ON, now);
		return;
	}
	if (raw == EVSE_CP_D && e->cfg.reject_state_d) {
		e->errors |= EVSE_ERR_STATE_D;
		return;
	}
	/* SOLL-LOCK-40 */
	if (e->lock_cfg.d4 && e->lock_cfg.d6 && e->ld_mode == EVSE_LD_PERMANENT) {
		if (!e->din.ld) {
			if (e->ld_lost_since == EVSE_MS_NONE) {
				e->ld_lost_since = now;
			}
			if (evse_elapsed_gt(now, e->ld_lost_since, EVSE_LD_LOST_MS)) {
				e->errors |= EVSE_ERR_LD_LOST;
				contactor_off(e, now);
				e->relock_mode = true;
				e->relock_requested = false;
				e->ld_lost_since = EVSE_MS_NONE;
				goto_state(e, EVSE_S2_LOCKING, now);
			}
		} else {
			e->ld_lost_since = EVSE_MS_NONE;
		}
	}
}

static void st_stopping(struct evse *e, enum evse_cp_state raw, evse_ms_t now)
{
	/* SOLL-SM-71 */
	if (raw != EVSE_CP_C && raw != EVSE_CP_D) {
		contactor_off(e, now);
	} else if (evse_elapsed_ge(now, e->stop_since, EVSE_STOP_TIMEOUT_MS)) {
		contactor_off(e, now);
	}
	if (!e->contactor) {
		goto_state(e, e->stop_target, now);
	}
}

static void st_unlocking(struct evse *e, enum evse_cp_state raw, enum evse_lock_result res,
			 evse_ms_t now)
{
	if (e->contactor) {
		contactor_off(e, now);
		e->errors |= EVSE_ERR_INTERNAL; /* SOLL-LOCK-26: darf nicht vorkommen */
		return;
	}
	if (res != EVSE_LOCK_RES_NONE) {
		if (e->unlock_fail_relocking) {
			e->unlock_fail_relocking = false;
		} else if (res == EVSE_LOCK_RES_OK && !e->last_lock_wish) {
			e->errors &= ~EVSE_ERR_UNLOCK;
			e->unlock_attempts = 0;
			goto_state(e, raw == EVSE_CP_E ? EVSE_S9_ERROR : EVSE_S1_IDLE, now);
			return;
		} else if (res == EVSE_LOCK_RES_FAIL && !e->last_lock_wish) {
			e->errors |= EVSE_ERR_UNLOCK; /* SOLL-LOCK-22 */
			if (++e->unlock_attempts >= EVSE_UNLOCK_ATTEMPTS) {
				goto_state(e, EVSE_S9_ERROR, now);
				return;
			}
			e->unlock_fail_relocking = lock_req(e, true, now);
			return;
		}
	}
	if (evse_lock_busy(&e->lock)) {
		return;
	}
	/* SOLL-SM-81 */
	if (dip(e, 9) && e->rt.lock_request && !e->rt.unlock_request && raw != EVSE_CP_E &&
	    e->unlock_attempts == 0) {
		goto_state(e, EVSE_S2_LOCKING, now);
		return;
	}
	if (!locked(e)) {
		goto_state(e, raw == EVSE_CP_E ? EVSE_S9_ERROR : EVSE_S1_IDLE, now);
		return;
	}
	(void)lock_req(e, false, now);
}

static void st_error(struct evse *e, enum evse_cp_state raw, enum evse_lock_result res,
		     evse_ms_t now)
{
	if (raw == EVSE_CP_A) {
		clear_at_a(e); /* SOLL-SM-90 */
	}
	rcm_retest(e, raw, now);

	/* Entriegeln bei A auch mit anstehendem Schutzfehler (Schuetz ist aus) */
	if (res == EVSE_LOCK_RES_FAIL) {
		e->unlock_attempts++;
	}
	if (raw == EVSE_CP_A && e->lock_cfg.d4 && !dip(e, 9) && locked(e) &&
	    !evse_lock_busy(&e->lock) && e->unlock_attempts < EVSE_UNLOCK_ATTEMPTS) {
		(void)lock_req(e, false, now);
	}

	if ((e->errors & EVSE_ERR_CONTACTOR) || e->avail_missing) {
		goto_state(e, EVSE_S10_UNAVAILABLE, now); /* SOLL-SM-92 */
		return;
	}
	if (e->errors != 0 || e->rcm.test == EVSE_RCM_TEST_RUNNING || evse_lock_busy(&e->lock)) {
		return;
	}
	if (raw == EVSE_CP_A || cp_is_bcd(raw)) {
		goto_state(e, EVSE_S1_IDLE, now); /* SOLL-SM-90/91 */
	}
}

static void st_unavailable(struct evse *e, evse_ms_t now)
{
	if (!e->avail_missing && !(e->errors & EVSE_ERR_CONTACTOR)) {
		goto_state(e, e->errors ? EVSE_S9_ERROR : EVSE_S1_IDLE, now); /* SOLL-SM-100/101 */
	}
}

/* --------------------------------------------------------------- Ausgaben */

static uint16_t cp_output(const struct evse *e, evse_ms_t now)
{
	switch (e->state) {
	case EVSE_S5_PWM_ON:
		if (e->wake_phase == 1) {
			return EVSE_DUTY_F;
		}
		if (e->wake_phase == 2) {
			return EVSE_DUTY_OFF;
		}
		return e->cur.duty;
	case EVSE_S6_CHARGING:
		return e->cur.duty;
	case EVSE_S10_UNAVAILABLE:
		return EVSE_DUTY_F;
	case EVSE_S9_ERROR:
		/* F7: 3 s -12 V, dann +12 V zur Abstecken-Erkennung */
		if ((e->errors & EVSE_ERR_DIODE) && e->diode_since != EVSE_MS_NONE &&
		    (now - e->diode_since) < EVSE_DIODE_NEG_MS) {
			return EVSE_DUTY_F;
		}
		return EVSE_DUTY_OFF;
	default:
		return EVSE_DUTY_OFF;
	}
}

static bool led_level(enum evse_led_mode m, evse_ms_t now)
{
	switch (m) {
	case EVSE_LEDM_ON:
		return true;
	case EVSE_LEDM_1HZ:
		return (now % 1000) < 500;
	case EVSE_LEDM_2HZ:
		return (now % 500) < 250;
	case EVSE_LEDM_5HZ:
		return (now % 200) < 100;
	default:
		return false;
	}
}

static void update_outputs(struct evse *e, evse_ms_t now)
{
	struct evse_outputs *o = &e->out;
	uint16_t duty = cp_output(e, now);

	if (duty != e->cp_duty) {
		evse_cp_output_changed(&e->cp); /* SOLL-CP-05 */
		if (duty_is_pwm(duty) && !duty_is_pwm(e->cp_duty)) {
			e->pwm_since = now;
		} else if (!duty_is_pwm(duty)) {
			e->pwm_since = EVSE_MS_NONE;
		}
		e->cp_duty = duty;
	}

	/* Schuetz nur in S6/S7 und ohne Schutzfehler (SOLL-PROT-01) */
	if ((e->state != EVSE_S6_CHARGING && e->state != EVSE_S7_STOPPING) ||
	    (e->errors & EVSE_ERR_CLASS_P)) {
		contactor_off(e, now);
	}

	o->cp_duty = e->cp_duty;
	o->contactor = e->contactor;
	o->lock_pulse = e->lock.out_lock;
	o->unlock_pulse = e->lock.out_unlock;
	o->rcm_test = e->rcm.out_test;
	o->pe1 = false; /* F15 */

	for (int i = 0; i < EVSE_DOUT_COUNT; i++) {
		o->dout[i] = evse_dout_eval(e, e->cfg.out_function[i], now);
	}

	/* 11.2 */
	o->led_mode[EVSE_LED_POWER] = EVSE_LEDM_1HZ;
	o->led_mode[EVSE_LED_ERROR] = (e->errors || e->state == EVSE_S9_ERROR ||
				       e->state == EVSE_S10_UNAVAILABLE)
					      ? EVSE_LEDM_ON
					      : EVSE_LEDM_OFF;
	if (locked(e)) {
		o->led_mode[EVSE_LED_CONNECT] = EVSE_LEDM_ON;
	} else if (dip(e, 1) && e->pp.cable_a > 0) {
		o->led_mode[EVSE_LED_CONNECT] = EVSE_LEDM_1HZ;
	} else {
		o->led_mode[EVSE_LED_CONNECT] = EVSE_LEDM_OFF;
	}
	if (e->contactor) {
		o->led_mode[EVSE_LED_READY] = EVSE_LEDM_ON;
	} else if (duty_is_pwm(e->cp_duty)) {
		o->led_mode[EVSE_LED_READY] = EVSE_LEDM_2HZ;
	} else if (cp_is_bcd(e->status)) {
		o->led_mode[EVSE_LED_READY] = EVSE_LEDM_1HZ;
	} else {
		o->led_mode[EVSE_LED_READY] = EVSE_LEDM_OFF;
	}
	for (int i = 0; i < EVSE_LED_COUNT; i++) {
		o->led[i] = led_level(o->led_mode[i], now);
	}
}

/* ------------------------------------------------------------ Hauptzyklus */

void evse_step(struct evse *e, const struct evse_inputs *in, evse_ms_t now)
{
	if (e->safe) {
		return;
	}
	if (in->new_block) {
		measure(e, in, now);
	}
	protect(e, in, now);
	update_errors(e, now);
	update_release_and_current(e);
	update_status(e, now);

	int ld_ch;
	(void)evse_din_ld_source(&e->cfg.din, &ld_ch);
	bool ld_raw = ld_ch >= 0 && e->din.ch[ld_ch].hyst;

	evse_lock_update(&e->lock, &e->lock_cfg, e->din.ld, ld_raw, in->new_block, now);
	enum evse_lock_result res = evse_lock_take_result(&e->lock);
	enum evse_cp_state raw = evse_cp_raw(&e->cp);

	if (e->state == EVSE_S0_INIT) {
		st_init(e, res, now);
	} else if (e->state >= EVSE_S2_LOCKING && e->state <= EVSE_S8_UNLOCKING &&
		   prelude(e, raw, now)) {
		/* Reaktion nach Vorrangliste */
	} else {
		switch (e->state) {
		case EVSE_S1_IDLE:
			st_idle(e, raw, now);
			break;
		case EVSE_S2_LOCKING:
			st_locking(e, raw, res, now);
			break;
		case EVSE_S3_WAIT:
			st_wait(e, raw, now);
			break;
		case EVSE_S4_SELFTEST:
			st_selftest(e, now);
			break;
		case EVSE_S5_PWM_ON:
			st_pwm_on(e, raw, now);
			break;
		case EVSE_S6_CHARGING:
			st_charging(e, raw, now);
			break;
		case EVSE_S7_STOPPING:
			st_stopping(e, raw, now);
			break;
		case EVSE_S8_UNLOCKING:
			st_unlocking(e, raw, res, now);
			break;
		case EVSE_S9_ERROR:
			st_error(e, raw, res, now);
			break;
		case EVSE_S10_UNAVAILABLE:
			st_unavailable(e, now);
			break;
		default:
			break;
		}
	}
	update_outputs(e, now);
}

void evse_safe_state(struct evse *e, evse_ms_t now)
{
	/* SOLL-RT-20: zuerst Schuetz, dann CP -12 V, Aktor in Ruhe */
	e->contactor = false;
	e->contactor_off_since = now;
	e->out.contactor = false;
	e->cp_duty = EVSE_DUTY_F;
	e->out.cp_duty = EVSE_DUTY_F;
	evse_lock_abort(&e->lock, now);
	evse_rcm_selftest_abort(&e->rcm);
	e->out.lock_pulse = false;
	e->out.unlock_pulse = false;
	e->out.rcm_test = false;
	e->safe = true;
}

bool evse_ack_contactor_fault(struct evse *e)
{
	if (e->state != EVSE_S1_IDLE && e->state != EVSE_S9_ERROR &&
	    e->state != EVSE_S10_UNAVAILABLE) {
		return false;
	}
	return evse_ctm_acknowledge(&e->ctm);
}
