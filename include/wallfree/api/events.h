/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Ereignisse an die Schnittstellen (SOLL-ERR-10): Fehler, Zustandswechsel,
 * Verriegelungsergebnisse, Schuetzschaltungen, Zaehlerzustand. Erkennung durch Vergleich
 * zweier Momentaufnahmen der Ladelogik, Ringpuffer mit fortlaufender
 * Nummer (Luecken = verlorene Ereignisse), JSON fuer das WebSocket-Topic
 * evse/event. Reines C, nicht threadsicher (Aufrufer sperrt).
 */
#ifndef WALLFREE_API_EVENTS_H_
#define WALLFREE_API_EVENTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wallfree/api/json.h>
#include <wallfree/evse/charger.h>

#ifdef __cplusplus
extern "C" {
#endif

enum wf_ev_type {
	WF_EV_STATE = 0, /**< Automatenzustand: a = vorher, b = nachher (S0..S10) */
	WF_EV_ERROR,     /**< Fehler: set/cleared */
	WF_EV_LOCK,      /**< Verriegelung erreicht Endzustand: a = lock_state (1, 3, 5) */
	WF_EV_CONTACTOR, /**< Schuetz-Soll: a = 1 ein, 0 aus */
	WF_EV_METER,     /**< Zaehler (SOLL-MTR-13/14): a = enum meter_status, b = 1 Zaehlerstand gesunken */
	WF_EV_UPDATE,    /**< Ergebnis eines Updates beim Start (SOLL-BOOT-33): a = 1 Erfolg, b = Modus */
};

/** Was die Erkennung vergleicht. */
struct wf_ev_snap {
	uint8_t state;
	uint8_t lock;
	bool contactor;
	evse_err_t errors;
};

struct wf_event {
	uint32_t seq;      /**< ab 1, vom Ringpuffer vergeben */
	evse_ms_t t;
	uint8_t type;      /**< enum wf_ev_type */
	uint8_t a;
	uint8_t b;
	evse_err_t set;
	evse_err_t cleared;
};

#define WF_EV_MAX_DIFF 4 /* hoechstens ein Ereignis je Typ und Vergleich */

void wf_ev_snap_take(const struct evse *e, struct wf_ev_snap *s);

/**
 * Ereignisse zwischen zwei Momentaufnahmen, Reihenfolge: Fehler, Schuetz,
 * Verriegelung, Zustand. @return Anzahl (0..WF_EV_MAX_DIFF), seq noch 0.
 */
size_t wf_ev_diff(const struct wf_ev_snap *old, const struct wf_ev_snap *cur, evse_ms_t t,
		  struct wf_event out[WF_EV_MAX_DIFF]);

#define WF_EV_RING 16

struct wf_ev_ring {
	struct wf_event ev[WF_EV_RING];
	uint32_t next_seq; /**< naechste zu vergebende Nummer (ab 1) */
};

void wf_ev_ring_init(struct wf_ev_ring *r);

/** Anhaengen; das aelteste Ereignis faellt bei vollem Puffer heraus. */
void wf_ev_ring_push(struct wf_ev_ring *r, struct wf_event ev);

/**
 * Naechstes Ereignis ab Nummer @p *seq lesen. Ist es schon ueberschrieben,
 * wird das aelteste vorhandene geliefert (Luecke an seq erkennbar).
 * @return false, wenn keins vorliegt; sonst *seq = gelieferte Nummer + 1.
 */
bool wf_ev_ring_get(const struct wf_ev_ring *r, uint32_t *seq, struct wf_event *out);

/** Payload als JSON-Objekt, z. B. {"seq":3,"t_ms":1200,"type":"state","from":1,"to":2}. */
void wf_ev_json(const struct wf_event *ev, struct wf_jbuf *b);

#ifdef __cplusplus
}
#endif

#endif
