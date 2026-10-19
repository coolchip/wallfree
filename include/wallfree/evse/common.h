/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Gemeinsame Typen der Ladelogik.
 *
 * Die Ladelogik ist reines C99 ohne Abhaengigkeit von Zephyr oder Hardware
 * (SOLL-GEN-05). Zeit wird als monotone Millisekunde uebergeben (SOLL-RT-06),
 * Spannungen in der skalierten Einheit "mV" des Messblocks, Stroeme in mA
 * (E21).
 */
#ifndef WALLFREE_EVSE_COMMON_H_
#define WALLFREE_EVSE_COMMON_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Monotone Zeit in Millisekunden. */
typedef int64_t evse_ms_t;

/** Kennzeichen "kein Zeitpunkt gesetzt". */
#define EVSE_MS_NONE INT64_MIN

/** true, wenn seit @p since mehr als @p dur ms vergangen sind. */
static inline bool evse_elapsed_gt(evse_ms_t now, evse_ms_t since, evse_ms_t dur)
{
	return since != EVSE_MS_NONE && (now - since) > dur;
}

/** true, wenn seit @p since mindestens @p dur ms vergangen sind. */
static inline bool evse_elapsed_ge(evse_ms_t now, evse_ms_t since, evse_ms_t dur)
{
	return since != EVSE_MS_NONE && (now - since) >= dur;
}

/**
 * Entprellung "Abweichung laenger als hold": Der Wert uebernimmt den
 * aktuellen Kandidaten, sobald dieser ununterbrochen laenger als hold vom
 * bisherigen Wert abweicht (SOLL-CP-03). Uebernommen wird der Kandidat zum
 * Uebernahmezeitpunkt, nicht der Wert bei Fristbeginn.
 */
struct evse_debounce {
	int value;
	evse_ms_t dev_since;
};

void evse_debounce_init(struct evse_debounce *d, int value);

/** @return true, wenn der Wert in diesem Aufruf gewechselt hat. */
bool evse_debounce_update(struct evse_debounce *d, int candidate, evse_ms_t now,
			  evse_ms_t hold);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_COMMON_H_ */
