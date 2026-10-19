/* SPDX-License-Identifier: Apache-2.0 */
/* PP-Auswertung, SOLL-PP-01..06 */
#include <wallfree/evse/pp.h>

void evse_pp_init(struct evse_pp *pp)
{
	pp->cable_a = 0;
	pp->candidate = 0;
	pp->cand_since = EVSE_MS_NONE;
	pp->invalid = false;
	pp->reject = false;
	pp->unplugged_event = false;
}

int16_t evse_pp_classify(int32_t mv)
{
	/* Schwellen des Originals in dessen Skalierung (HB Tab. 6-4 Reihenfolge) */
	if (mv >= 7401) {
		return 0;
	}
	if (mv >= 5331) {
		return 13;
	}
	if (mv >= 2821) {
		return 20;
	}
	if (mv >= 1521) {
		return 32;
	}
	if (mv >= 821) {
		return 63;
	}
	return EVSE_PP_INVALID;
}

static void update_reject(struct evse_pp *pp, const struct evse_pp_config *cfg)
{
	/* SOLL-PP-05: nur mit D1 und D2 */
	pp->reject = cfg->d2_reject &&
		     (pp->cable_a == 13 || (!cfg->d3_only13 && pp->cable_a == 20));
}

void evse_pp_update(struct evse_pp *pp, const struct evse_pp_config *cfg, int32_t mv,
		    enum evse_cp_state cp_raw, evse_ms_t now)
{
	pp->unplugged_event = false;

	if (!cfg->d1_proximity) {
		/* SOLL-PP-01: Ladefall C, Kabelwert = Drehschalter */
		pp->cable_a = cfg->rotary_a;
		pp->candidate = cfg->rotary_a;
		pp->cand_since = EVSE_MS_NONE;
		pp->invalid = false;
		pp->reject = false;
		return;
	}

	int16_t cand = evse_pp_classify(mv);

	if (cand != pp->candidate || pp->cand_since == EVSE_MS_NONE) {
		pp->candidate = cand;
		pp->cand_since = now;
	} else if (evse_elapsed_gt(now, pp->cand_since, EVSE_PP_HOLD_MS)) {
		/* SOLL-PP-03: uebernehmen, danach etwa jede Sekunde neu bewerten */
		pp->cable_a = cand;
		pp->invalid = cand == EVSE_PP_INVALID ||
			      (cand == 0 && cp_raw != EVSE_CP_A && cp_raw != EVSE_CP_F);
		pp->unplugged_event = cand == 0;
		pp->cand_since = now;
	}
	update_reject(pp, cfg);
}
