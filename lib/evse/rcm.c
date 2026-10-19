/* SPDX-License-Identifier: Apache-2.0 */
/* Fehlerstrom und RCM-Selbsttest, SOLL-PROT-04..13 */
#include <wallfree/evse/rcm.h>

void evse_rcm_init(struct evse_rcm *r)
{
	r->faults = 0;
	r->test = EVSE_RCM_TEST_IDLE;
	r->test_since = EVSE_MS_NONE;
	r->test_step = 0;
	r->out_test = false;
}

uint8_t evse_rcm_check(const struct evse_rcm_levels *lv)
{
	/* SOLL-PROT-07: alle zutreffenden Bits */
	uint8_t f = 0;

	if (lv->err) {
		f |= EVSE_RCM_F_SENSOR;
	}
	if (lv->sw2) {
		f |= EVSE_RCM_F_DC6;
	}
	if (lv->sw1) {
		f |= EVSE_RCM_F_AC30;
	}
	return f;
}

bool evse_rcm_monitor(struct evse_rcm *r, const struct evse_rcm_levels *lv)
{
	if (r->test == EVSE_RCM_TEST_RUNNING) {
		/* erwartete Ausloesungen bewertet der Selbsttest */
		return false;
	}
	uint8_t f = evse_rcm_check(lv);

	r->faults |= f;
	return f != 0;
}

bool evse_rcm_selftest_start(struct evse_rcm *r, const struct evse_rcm_levels *lv, evse_ms_t now)
{
	if (evse_rcm_check(lv) != 0) {
		r->faults |= evse_rcm_check(lv);
		r->test = EVSE_RCM_TEST_FAILED;
		return false;
	}
	r->test = EVSE_RCM_TEST_RUNNING;
	r->test_since = now;
	r->test_step = 0;
	r->out_test = false;
	return true;
}

static void fail(struct evse_rcm *r, uint8_t bits)
{
	r->faults |= bits | EVSE_RCM_F_SELFTEST;
	r->out_test = false;
	r->test = EVSE_RCM_TEST_FAILED;
}

void evse_rcm_selftest_update(struct evse_rcm *r, const struct evse_rcm_levels *lv, evse_ms_t now)
{
	if (r->test != EVSE_RCM_TEST_RUNNING) {
		return;
	}
	evse_ms_t t = now - r->test_since;

	/* ERR_OUT ist waehrend des ganzen Tests ein Fehler (jede Abweichung, SOLL-PROT-10) */
	if (lv->err) {
		fail(r, EVSE_RCM_F_SENSOR);
		return;
	}

	switch (r->test_step) {
	case 0: /* Vorbedingung bis 100 ms: nichts darf ausloesen */
		if (lv->sw1 || lv->sw2) {
			fail(r, 0);
			return;
		}
		if (t >= EVSE_RCM_T_ERRCHECK) {
			r->test_step = 1;
		}
		break;
	case 1:
		if (t >= EVSE_RCM_T_PULSE_ON) {
			r->out_test = true;
			r->test_step = 2;
		}
		break;
	case 2:
		if (t >= EVSE_RCM_T_PULSE_OFF) {
			r->out_test = false;
			r->test_step = 3;
		}
		break;
	case 3: /* M1 */
		if (t >= EVSE_RCM_T_SW2) {
			if (!lv->sw2) {
				fail(r, EVSE_RCM_F_DC6);
				return;
			}
			r->test_step = 4;
		}
		break;
	case 4: /* M2 */
		if (t >= EVSE_RCM_T_SW1) {
			if (!lv->sw1) {
				fail(r, EVSE_RCM_F_AC30);
				return;
			}
			r->test_step = 5;
		}
		break;
	case 5: /* M3 */
		if (t >= EVSE_RCM_T_NOT_BOTH) {
			if (lv->sw1 && lv->sw2) {
				fail(r, 0);
				return;
			}
			r->test_step = 6;
		}
		break;
	case 6: /* vollstaendige Rueckkehr (Abweichung von 1.27) */
		if (!lv->sw1 && !lv->sw2) {
			r->faults = 0; /* SOLL-PROT-12 */
			r->test = EVSE_RCM_TEST_PASSED;
		} else if (t >= EVSE_RCM_T_RELEASED) {
			fail(r, 0);
		}
		break;
	default:
		fail(r, 0);
		break;
	}
}

void evse_rcm_selftest_abort(struct evse_rcm *r)
{
	if (r->test == EVSE_RCM_TEST_RUNNING) {
		r->out_test = false;
		r->test = EVSE_RCM_TEST_IDLE;
	}
}
