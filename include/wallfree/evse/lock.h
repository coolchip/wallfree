/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Verriegelungszyklus, nicht blockierend (Abschnitt 8, SOLL-LOCK-01..17).
 *
 * Das Modul steuert nur einen Zyklus (Abstand, Impuls, LD-Auswertung).
 * Ablaufregeln (wann ver-/entriegelt wird, Fehlversuche, Entriegeln nur bei
 * Schuetz aus) liegen im Ladeautomaten.
 */
#ifndef WALLFREE_EVSE_LOCK_H_
#define WALLFREE_EVSE_LOCK_H_

#include <wallfree/evse/common.h>
#include <wallfree/evse/din.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Zustaende, Zahlenwerte wie WARP `lock_state`. */
enum evse_lock_state {
	EVSE_LOCK_INIT = 0,
	EVSE_LOCK_UNLOCKED = 1,
	EVSE_LOCK_LOCKING = 2,
	EVSE_LOCK_LOCKED = 3,
	EVSE_LOCK_UNLOCKING = 4,
	EVSE_LOCK_ERROR = 5,
};

enum evse_lock_result {
	EVSE_LOCK_RES_NONE = 0,
	EVSE_LOCK_RES_OK,
	EVSE_LOCK_RES_FAIL,
};

#define EVSE_LOCK_SETTLE_MS 500 /* nach dem Impuls bis zur LD-Auswertung (mit D6) */
#define EVSE_LOCK_END_MS    10
#define EVSE_LOCK_LD_BLOCKS 2   /* LD gepulst: mindestens 2 Bloecke (>= 10 ms) aktiv */

struct evse_lock_config {
	bool d4;                   /**< Verriegelung aktiv */
	bool d6;                   /**< LD auswerten */
	enum evse_ld_mode ld_mode; /**< aus evse_din_ld_source() */
	uint16_t lock_pulse_ms;    /**< Feld 525, 100..3000 */
	uint16_t unlock_pulse_ms;  /**< Feld 526, 100..3000 */
	uint16_t cycle_gap_ms;     /**< Feld 527, 500..10000 */
	bool unlock_check;         /**< F27: Fahrt auch beim Entriegeln pruefen */
};

enum evse_lock_phase {
	EVSE_LOCK_PH_IDLE = 0,
	EVSE_LOCK_PH_GAP,
	EVSE_LOCK_PH_PULSE,
	EVSE_LOCK_PH_SETTLE,
	EVSE_LOCK_PH_END,
};

struct evse_lock {
	enum evse_lock_state state;
	enum evse_lock_phase phase;
	bool target;               /**< true = verriegeln */
	enum evse_lock_state before; /**< Endzustand vor dem Zyklus */
	evse_ms_t phase_since;
	evse_ms_t last_end;
	uint8_t ld_high_blocks;
	bool ld_moved;             /**< gepulste Rueckmeldung erkannt */
	bool success;
	enum evse_lock_result result; /**< Ereignis, bis evse_lock_take_result() */

	/* Ausgaenge: true = Impuls aktiv (Pin Low) */
	bool out_lock;             /**< PA9 */
	bool out_unlock;           /**< PA10 */
};

/** @param stored gespeicherter Endzustand (UNLOCKED oder LOCKED). */
void evse_lock_init(struct evse_lock *l, enum evse_lock_state stored);

/**
 * Zyklus anfordern. Ohne D4 sofortiger Zustandswechsel (SOLL-LOCK-01).
 * @return false, wenn bereits ein Zyklus laeuft.
 */
bool evse_lock_request(struct evse_lock *l, const struct evse_lock_config *cfg, bool lock,
		       evse_ms_t now);

/**
 * Fortschreiben.
 * @param ld_perm   entprellter LD-Pegel (Code 3)
 * @param ld_raw    LD-Pegel nach Hysterese des Code-22-Kanals
 * @param new_block true, wenn seit dem letzten Aufruf ein neuer Messblock vorliegt
 */
void evse_lock_update(struct evse_lock *l, const struct evse_lock_config *cfg, bool ld_perm,
		      bool ld_raw, bool new_block, evse_ms_t now);

/** Sicherer Zustand: Impuls sofort beenden, Zyklus fehlgeschlagen (SOLL-LOCK-17). */
void evse_lock_abort(struct evse_lock *l, evse_ms_t now);

static inline bool evse_lock_busy(const struct evse_lock *l)
{
	return l->phase != EVSE_LOCK_PH_IDLE;
}

/** Ergebnis des letzten Zyklus abholen (einmalig). */
enum evse_lock_result evse_lock_take_result(struct evse_lock *l);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_LOCK_H_ */
