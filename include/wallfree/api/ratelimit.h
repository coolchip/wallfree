/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Bremse gegen Passwort-Durchprobieren (SOLL-API-07): je Absenderadresse
 * nach WF_RL_FREE Fehlversuchen Sperre, beginnend mit WF_RL_LOCK_MS und bei
 * jedem weiteren Fehlversuch verdoppelt bis WF_RL_LOCK_MAX_MS. Ein Erfolg
 * setzt die Adresse zurueck. Gesperrt wird nur die eine Adresse, damit ein
 * Angreifer andere Clients (evcc) nicht aussperren kann.
 */
#ifndef WALLFREE_API_RATELIMIT_H_
#define WALLFREE_API_RATELIMIT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_RL_SLOTS        8
#define WF_RL_FREE         5
#define WF_RL_LOCK_MS      10000
#define WF_RL_LOCK_MAX_MS  300000
#define WF_RL_FORGET_MS    3600000 /* Fehlversuche verfallen nach einer Stunde Ruhe */

struct wf_rl_slot {
	uint32_t addr;
	uint8_t fails;
	bool used;
	int64_t last_ms;
	int64_t until_ms;
};

struct wf_rl {
	struct wf_rl_slot s[WF_RL_SLOTS];
};

void wf_rl_init(struct wf_rl *rl);

/** Gesperrt? @return verbleibende ms (> 0) oder 0 */
int64_t wf_rl_blocked(struct wf_rl *rl, uint32_t addr, int64_t now);

/** Fehlversuch vermerken (falsches Passwort, nicht fehlende Anmeldung). */
void wf_rl_fail(struct wf_rl *rl, uint32_t addr, int64_t now);

/** Erfolgreiche Anmeldung. */
void wf_rl_ok(struct wf_rl *rl, uint32_t addr);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_RATELIMIT_H_ */
