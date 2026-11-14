/* SPDX-License-Identifier: Apache-2.0 */
/* Schuetzueberwachung, SOLL-PROT-20..23 */
#include <wallfree/evse/ctm.h>

bool evse_ctm_mode_valid(int mode, bool meter_available)
{
	if (mode == EVSE_CTM_METER) {
		return meter_available;
	}
	return mode >= EVSE_CTM_OFF && mode <= EVSE_CTM_NC;
}

void evse_ctm_init(struct evse_ctm *c)
{
	c->bad_since = EVSE_MS_NONE;
	c->bad = false;
	c->fault = false;
	c->fault_on = false;
	c->sense = false;
}

static bool detect(enum evse_ctm_mode mode, const struct evse_ctm_in *in, evse_ms_t now)
{
	switch (mode) {
	case EVSE_CTM_SENSE:
		return in->last_sense_edge != EVSE_MS_NONE &&
		       (now - in->last_sense_edge) < EVSE_CTM_SENSE_EDGE_MS;
	case EVSE_CTM_NO:
		return in->ct_no;
	case EVSE_CTM_NC:
		return !in->ct_nc;
	case EVSE_CTM_METER:
		/* E4: jede Phase einzeln */
		if (!in->meter_valid) {
			return false;
		}
		for (int i = 0; i < 3; i++) {
			if (in->phase_v[i] > EVSE_CTM_PHASE_V) {
				return true;
			}
		}
		return false;
	default:
		return false;
	}
}

void evse_ctm_update(struct evse_ctm *c, enum evse_ctm_mode mode, const struct evse_ctm_in *in,
		     evse_ms_t now)
{
	c->sense = detect(EVSE_CTM_SENSE, in, now);
	bool check;
	evse_ms_t fault_ms;

	if (in->contactor_on) {
		/* SOLL-PROT-25: Rueckmeldung muss kommen (Modi 1..3) */
		check = mode >= EVSE_CTM_SENSE && mode <= EVSE_CTM_NC &&
			in->on_since != EVSE_MS_NONE &&
			evse_elapsed_ge(now, in->on_since, EVSE_CTM_ON_DELAY_MS);
		c->bad = check && !detect(mode, in, now);
		fault_ms = EVSE_CTM_ON_FAULT_MS;
	} else {
		/* SOLL-PROT-21: nach dem Aus darf keine Rueckmeldung mehr kommen */
		check = in->off_since != EVSE_MS_NONE &&
			evse_elapsed_ge(now, in->off_since, EVSE_CTM_CHECK_DELAY_MS);
		c->bad = check && detect(mode, in, now);
		fault_ms = EVSE_CTM_FAULT_MS;
	}
	if (!c->bad) {
		c->bad_since = EVSE_MS_NONE;
		return;
	}
	if (c->bad_since == EVSE_MS_NONE) {
		c->bad_since = now;
	}
	if (evse_elapsed_ge(now, c->bad_since, fault_ms) && !c->fault) {
		c->fault = true;
		c->fault_on = in->contactor_on;
	}
}

bool evse_ctm_acknowledge(struct evse_ctm *c)
{
	if (c->bad) {
		return false;
	}
	c->fault = false;
	c->fault_on = false;
	return true;
}
