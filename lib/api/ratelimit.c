/* SPDX-License-Identifier: Apache-2.0 */
/* Bremse gegen Passwort-Durchprobieren, siehe include/wallfree/api/ratelimit.h */
#include <string.h>
#include <wallfree/api/ratelimit.h>

void wf_rl_init(struct wf_rl *rl)
{
	memset(rl, 0, sizeof(*rl));
}

static struct wf_rl_slot *find(struct wf_rl *rl, uint32_t addr, int64_t now)
{
	for (int i = 0; i < WF_RL_SLOTS; i++) {
		struct wf_rl_slot *s = &rl->s[i];

		if (s->used && s->addr == addr) {
			if (now - s->last_ms > WF_RL_FORGET_MS && now >= s->until_ms) {
				s->used = false; /* verfallen */
				return NULL;
			}
			return s;
		}
	}
	return NULL;
}

int64_t wf_rl_blocked(struct wf_rl *rl, uint32_t addr, int64_t now)
{
	struct wf_rl_slot *s = find(rl, addr, now);

	return (s != NULL && now < s->until_ms) ? s->until_ms - now : 0;
}

void wf_rl_fail(struct wf_rl *rl, uint32_t addr, int64_t now)
{
	struct wf_rl_slot *s = find(rl, addr, now);

	if (s == NULL) {
		/* freier Platz, sonst der am laengsten ruhige ohne laufende Sperre,
		 * sonst der am laengsten ruhige ueberhaupt */
		struct wf_rl_slot *best = NULL;

		for (int i = 0; i < WF_RL_SLOTS; i++) {
			struct wf_rl_slot *c = &rl->s[i];

			if (!c->used) {
				best = c;
				break;
			}
			bool c_locked = now < c->until_ms;
			bool b_locked = best != NULL && now < best->until_ms;

			if (best == NULL || (b_locked && !c_locked) ||
			    (b_locked == c_locked && c->last_ms < best->last_ms)) {
				best = c;
			}
		}
		s = best;
		memset(s, 0, sizeof(*s));
		s->used = true;
		s->addr = addr;
	}
	s->last_ms = now;
	if (s->fails < 255) {
		s->fails++;
	}
	if (s->fails >= WF_RL_FREE) {
		int64_t lock = WF_RL_LOCK_MS;

		for (int k = WF_RL_FREE; k < s->fails && lock < WF_RL_LOCK_MAX_MS; k++) {
			lock *= 2;
		}
		if (lock > WF_RL_LOCK_MAX_MS) {
			lock = WF_RL_LOCK_MAX_MS;
		}
		s->until_ms = now + lock;
	}
}

void wf_rl_ok(struct wf_rl *rl, uint32_t addr)
{
	for (int i = 0; i < WF_RL_SLOTS; i++) {
		if (rl->s[i].used && rl->s[i].addr == addr) {
			rl->s[i].used = false;
		}
	}
}
