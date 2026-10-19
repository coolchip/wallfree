/* SPDX-License-Identifier: Apache-2.0 */
/* Verriegelungszyklus, SOLL-LOCK-01..17 */
#include <wallfree/evse/lock.h>

void evse_lock_init(struct evse_lock *l, enum evse_lock_state stored)
{
	l->state = stored == EVSE_LOCK_LOCKED ? EVSE_LOCK_LOCKED : EVSE_LOCK_UNLOCKED;
	l->phase = EVSE_LOCK_PH_IDLE;
	l->target = false;
	l->before = l->state;
	l->phase_since = EVSE_MS_NONE;
	l->last_end = EVSE_MS_NONE;
	l->ld_high_blocks = 0;
	l->ld_moved = false;
	l->success = false;
	l->result = EVSE_LOCK_RES_NONE;
	l->out_lock = false;
	l->out_unlock = false;
}

static enum evse_lock_state final_state(bool locked)
{
	return locked ? EVSE_LOCK_LOCKED : EVSE_LOCK_UNLOCKED;
}

bool evse_lock_request(struct evse_lock *l, const struct evse_lock_config *cfg, bool lock,
		       evse_ms_t now)
{
	if (evse_lock_busy(l)) {
		return false;
	}
	l->target = lock;
	l->before = l->state;
	if (!cfg->d4) {
		/* SOLL-LOCK-01: keine Impulse */
		l->state = final_state(lock);
		l->result = EVSE_LOCK_RES_OK;
		return true;
	}
	l->state = lock ? EVSE_LOCK_LOCKING : EVSE_LOCK_UNLOCKING;
	l->phase = EVSE_LOCK_PH_GAP;
	l->phase_since = now;
	return true;
}

static void enter(struct evse_lock *l, enum evse_lock_phase ph, evse_ms_t now)
{
	l->phase = ph;
	l->phase_since = now;
}

static void sample_pulsed(struct evse_lock *l, bool ld_raw)
{
	/* SOLL-LOCK-14: mindestens 2 Bloecke aktiv, danach wieder inaktiv */
	if (ld_raw) {
		if (l->ld_high_blocks < UINT8_MAX) {
			l->ld_high_blocks++;
		}
	} else {
		if (l->ld_high_blocks >= EVSE_LOCK_LD_BLOCKS) {
			l->ld_moved = true;
		}
		l->ld_high_blocks = 0;
	}
}

static bool evaluate(const struct evse_lock *l, const struct evse_lock_config *cfg, bool ld_perm)
{
	if (!cfg->d6) {
		return true; /* SOLL-LOCK-12 */
	}
	switch (cfg->ld_mode) {
	case EVSE_LD_PERMANENT:
		return l->target ? ld_perm : !ld_perm; /* SOLL-LOCK-13 */
	case EVSE_LD_PULSED:
		if (l->target || cfg->unlock_check) {
			return l->ld_moved; /* SOLL-LOCK-14, F27 */
		}
		return true;
	default:
		return false; /* D6 ohne LD-Eingang: ERR-CONFIG im Automaten */
	}
}

void evse_lock_update(struct evse_lock *l, const struct evse_lock_config *cfg, bool ld_perm,
		      bool ld_raw, bool new_block, evse_ms_t now)
{
	uint16_t pulse = l->target ? cfg->lock_pulse_ms : cfg->unlock_pulse_ms;

	switch (l->phase) {
	case EVSE_LOCK_PH_IDLE:
		break;
	case EVSE_LOCK_PH_GAP:
		/* SOLL-LOCK-10 */
		if (l->last_end == EVSE_MS_NONE ||
		    evse_elapsed_ge(now, l->last_end, cfg->cycle_gap_ms)) {
			l->ld_high_blocks = 0;
			l->ld_moved = false;
			l->out_lock = l->target;
			l->out_unlock = !l->target;
			enter(l, EVSE_LOCK_PH_PULSE, now);
		}
		break;
	case EVSE_LOCK_PH_PULSE:
		if (new_block && cfg->d6 && cfg->ld_mode == EVSE_LD_PULSED) {
			sample_pulsed(l, ld_raw);
		}
		if (evse_elapsed_ge(now, l->phase_since, pulse)) {
			l->out_lock = false;
			l->out_unlock = false;
			if (cfg->d6) {
				enter(l, EVSE_LOCK_PH_SETTLE, now);
			} else {
				l->success = evaluate(l, cfg, ld_perm);
				enter(l, EVSE_LOCK_PH_END, now);
			}
		}
		break;
	case EVSE_LOCK_PH_SETTLE:
		if (evse_elapsed_ge(now, l->phase_since, EVSE_LOCK_SETTLE_MS)) {
			l->success = evaluate(l, cfg, ld_perm);
			enter(l, EVSE_LOCK_PH_END, now);
		}
		break;
	case EVSE_LOCK_PH_END:
		if (evse_elapsed_ge(now, l->phase_since, EVSE_LOCK_END_MS)) {
			/* Misserfolg: Zustand ist das Gegenteil des Ziels (Ist §9 Schritt 6) */
			l->state = final_state(l->success ? l->target : !l->target);
			l->result = l->success ? EVSE_LOCK_RES_OK : EVSE_LOCK_RES_FAIL;
			l->last_end = now;
			l->phase = EVSE_LOCK_PH_IDLE;
		}
		break;
	}
}

void evse_lock_abort(struct evse_lock *l, evse_ms_t now)
{
	l->out_lock = false;
	l->out_unlock = false;
	if (!evse_lock_busy(l)) {
		return;
	}
	/* Lage unbekannt: Zustand vor dem Zyklus behalten, Ergebnis Fehlschlag */
	l->state = l->before;
	l->phase = EVSE_LOCK_PH_IDLE;
	l->result = EVSE_LOCK_RES_FAIL;
	l->last_end = now;
}

enum evse_lock_result evse_lock_take_result(struct evse_lock *l)
{
	enum evse_lock_result r = l->result;

	l->result = EVSE_LOCK_RES_NONE;
	return r;
}
