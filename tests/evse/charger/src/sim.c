/* SPDX-License-Identifier: Apache-2.0 */
#include <string.h>
#include "sim.h"

void sim_init(struct sim *s, const struct evse_config *cfg)
{
	memset(s, 0, sizeof(*s));
	s->cfg = *cfg;
	s->now = 1000;
	s->car = EVSE_CP_A;
	s->diode = true;
	s->pp_mv = 9000; /* kein Kabel */
	s->rcm_healthy = true;
	s->test_pulse_at = EVSE_MS_NONE;
	s->ld_model = LD_MODEL_KUESTER;
	s->pulse_start = EVSE_MS_NONE;
	s->last_edge = EVSE_MS_NONE;
}

void sim_start(struct sim *s)
{
	evse_init(&s->e, &s->cfg, s->actuator_locked ? EVSE_LOCK_LOCKED : EVSE_LOCK_UNLOCKED,
		  s->now);
}

static int32_t level_mv(enum evse_cp_state st)
{
	switch (st) {
	case EVSE_CP_A:
		return 12000;
	case EVSE_CP_B:
		return 9000;
	case EVSE_CP_C:
		return 6000;
	case EVSE_CP_D:
		return 3000;
	default:
		return 0; /* E: Kurzschluss */
	}
}

static void model(struct sim *s, struct evse_inputs *in)
{
	const struct evse_outputs *o = &s->e.out;

	/* CP: Fahrzeugpegel am positiven Plateau, Diode haelt -12 V */
	in->cp_pos_mv = level_mv(s->car);
	in->cp_neg_mv = s->diode ? 12000 : in->cp_pos_mv;
	if (s->car == EVSE_CP_A) {
		in->cp_neg_mv = 12000;
	}
	in->pp_mv = s->pp_mv;
	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		in->din_mv[i] = s->din_mv[i];
	}
	in->supply_mv = 12000;

	/* Bender: SWITCH_2 330 ms, SWITCH_1 770 ms nach Testbeginn, Reclose 1,9 s */
	if (o->rcm_test && s->test_pulse_at == EVSE_MS_NONE) {
		s->test_pulse_at = s->now;
	}
	bool sw1 = false, sw2 = false;

	if (s->test_pulse_at != EVSE_MS_NONE && s->rcm_healthy) {
		evse_ms_t t = s->now - s->test_pulse_at;

		sw2 = t >= 330 && t < 1900;
		sw1 = t >= 770 && t < 1900;
		if (t >= 2500) {
			s->test_pulse_at = EVSE_MS_NONE;
		}
	} else if (s->test_pulse_at != EVSE_MS_NONE && s->now - s->test_pulse_at >= 2500) {
		s->test_pulse_at = EVSE_MS_NONE;
	}
	if (s->rcm_fault_current) {
		sw1 = sw2 = true;
	}
	in->rcm.sw1 = sw1;
	in->rcm.sw2 = sw2;
	in->rcm.err = false;

	/* Aktor: Fahrt 200 ms nach Impulsbeginn, Endlage danach */
	bool pulse = o->lock_pulse || o->unlock_pulse;

	if (pulse && !s->pulse_was) {
		s->pulse_start = s->now;
	}
	bool moving = pulse && !s->actuator_stuck && s->pulse_start != EVSE_MS_NONE &&
		      (s->now - s->pulse_start) < 200;

	if (pulse && !s->actuator_stuck && s->now - s->pulse_start >= 200) {
		s->actuator_locked = o->lock_pulse;
	}
	s->pulse_was = pulse;

	bool ld = false;

	if (s->ld_model == LD_MODEL_KUESTER) {
		ld = moving;
	} else if (s->ld_model == LD_MODEL_PERMANENT) {
		ld = s->actuator_locked && !s->ld_force_off;
	}
	in->din_mv[EVSE_DIN_LD] = ld ? 12000 : 0;

	/* Schuetz-Sense: 50 Hz hinter dem Schuetz */
	if ((o->contactor || s->welded) && (s->now % 20) == 0) {
		s->last_edge = s->now;
	}
	in->last_sense_edge = s->last_edge;
	in->new_block = (s->now % 8) == 0;
}

void sim_run(struct sim *s, int ms)
{
	for (int i = 0; i < ms; i++) {
		struct evse_inputs in = {0};

		s->now++;
		model(s, &in);
		evse_step(&s->e, &in, s->now);
		if (s->e.out.contactor && !s->contactor_prev) {
			s->contactor_on_count++;
		}
		s->contactor_prev = s->e.out.contactor;
	}
}

int sim_until_state(struct sim *s, enum evse_state st, int max_ms)
{
	for (int t = 0; t < max_ms; t++) {
		if (s->e.state == st) {
			return t;
		}
		sim_run(s, 1);
	}
	return s->e.state == st ? max_ms : -1;
}

void sim_boot(struct sim *s)
{
	sim_start(s);
	(void)sim_until_state(s, EVSE_S1_IDLE, 10000);
}
