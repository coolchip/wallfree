/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Ereignisprotokoll (SOLL-ERR-11): fehlgeschlagene Anmeldungen mit
 * Absenderadresse und Ereignisse (SOLL-ERR-10) in einem Ringpuffer, der Neustarts
 * uebersteht (Backup-SRAM, wf_alog_restore), JSON fuer GET /system/log. Jeder Eintrag
 * traegt die Nummer des Starts und, falls bekannt, die Uhrzeit (SOLL-ERR-12). Fortlaufende Nummer je Eintrag (Luecke =
 * herausgefallen). Aufeinanderfolgende Fehlversuche derselben Adresse
 * zaehlen im juengsten Eintrag weiter, damit Durchprobieren die Ereignisse
 * nicht verdraengt. Reines C, nicht threadsicher (Aufrufer sperrt).
 */
#ifndef WALLFREE_API_ALOG_H_
#define WALLFREE_API_ALOG_H_

#include <stdbool.h>
#include <stdint.h>
#include <wallfree/api/events.h>
#include <wallfree/api/json.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_ALOG_SIZE 48
#define WF_ALOG_MAGIC 0x574c4f47u /* "WLOG" */
#define WF_ALOG_VERSION 2

enum wf_alog_kind {
	WF_ALOG_AUTH_FAIL = 0, /**< falsches Passwort: addr, count */
	WF_ALOG_EVENT,         /**< Ereignis nach SOLL-ERR-10: ev */
	WF_ALOG_CLEARED,       /**< Protokoll geloescht (SOLL-ERR-13): addr des Ausloesers */
};

struct wf_alog_entry {
	uint32_t seq;   /**< ab 1, vom Puffer vergeben */
	int64_t t;      /**< ms seit Start; bei zusammengefassten Fehlversuchen der letzte */
	uint8_t kind;   /**< enum wf_alog_kind */
	uint16_t count; /**< Fehlversuche (bleibt bei 65535 stehen) */
	uint32_t addr;  /**< IPv4 in Host-Reihenfolge, 0 = unbekannt */
	uint16_t boot;  /**< Startnummer (wf_alog.boot beim Eintragen) */
	int64_t wall;   /**< Uhrzeit ms seit 1970, 0 = unbekannt */
	struct wf_event ev;
};

struct wf_alog {
	uint32_t magic;
	uint16_t version;
	uint16_t boot;      /**< Nummer des laufenden Starts, ab 1 */
	int64_t wall_base;  /**< Uhrzeit (ms seit 1970) bei Laufzeit 0, 0 = unbekannt */
	struct wf_alog_entry e[WF_ALOG_SIZE];
	uint32_t next_seq;
	uint32_t first_seq; /**< aeltester sichtbarer Eintrag (nach dem Loeschen) */
};

void wf_alog_init(struct wf_alog *l);

/**
 * Nach einem Start: Inhalt aus dem vorigen Lauf uebernehmen, wenn er stimmig ist
 * (Kennung, Version, Nummern), sonst neu anlegen. Zaehlt die Startnummer weiter,
 * Uhrzeit unbekannt. @return true, wenn Eintraege uebernommen wurden
 */
bool wf_alog_restore(struct wf_alog *l);

/**
 * Uhrzeit bekannt geworden (SNTP): ms seit 1970 bei Laufzeit 0, 0 = wieder unbekannt.
 * Eintraege des laufenden Starts ohne Uhrzeit erhalten sie nachtraeglich.
 */
void wf_alog_set_wall_base(struct wf_alog *l, int64_t wall_base);

/** Fehlversuch; zaehlt im juengsten Eintrag weiter, wenn der dieselbe Adresse betrifft. */
void wf_alog_auth_fail(struct wf_alog *l, uint32_t addr, int64_t t);

/** Ereignis; Zeit aus dem Ereignis. Der aelteste Eintrag faellt bei vollem Puffer heraus. */
void wf_alog_event(struct wf_alog *l, const struct wf_event *ev);

/**
 * Alle Eintraege verwerfen (SOLL-ERR-13); erster neuer Eintrag ist "geloescht" mit der
 * Absenderadresse, damit Loeschen selbst sichtbar bleibt. Die Nummern laufen weiter.
 */
void wf_alog_clear(struct wf_alog *l, uint32_t addr, int64_t t);

/** Eintrag ab Nummer @p *seq lesen, Verhalten wie wf_ev_ring_get. */
bool wf_alog_get(const struct wf_alog *l, uint32_t *seq, struct wf_alog_entry *out);

/**
 * Eintrag als JSON, z. B.
 * {"seq":4,"t_ms":5300,"kind":"auth_fail","addr":"192.168.1.20","count":3} oder
 * {"seq":5,"t_ms":6100,"kind":"event","event":{...wie evse/event...}} oder
 * {"seq":6,"t_ms":7000,"kind":"cleared","addr":"192.168.1.20"}.
 */
void wf_alog_entry_json(const struct wf_alog_entry *e, struct wf_jbuf *b);

#define WF_ALOG_DONE UINT32_MAX

/**
 * {"entries":[...]} in Teilen ausgeben, aelteste zuerst. @p cursor 0 beginnt
 * (mit Kopf); jeder Aufruf schreibt so viele ganze Eintraege, wie in @p b
 * passen (mindestens einen; einer, der allein nicht passt, wird
 * uebersprungen). @p b sollte einen groessten Eintrag fassen (~400 Byte),
 * sonst kann das Ergebnis ungueltig werden. @return naechster Cursor oder
 * WF_ALOG_DONE, wenn das Ende ("]}") geschrieben ist.
 */
uint32_t wf_alog_json(const struct wf_alog *l, uint32_t cursor, struct wf_jbuf *b);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_ALOG_H_ */
