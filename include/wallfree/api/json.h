/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Minimales JSON fuer die API: Ausgabe in einen festen Puffer und Lesen
 * flacher Objekte ({"name": Zahl|Text|true|false|null, ...}). Verschachtelte
 * Werte werden abgelehnt.
 */
#ifndef WALLFREE_API_JSON_H_
#define WALLFREE_API_JSON_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct wf_jbuf {
	char *p;
	size_t size;
	size_t len;
	bool overflow; /**< Ausgabe abgeschnitten */
};

void wf_jb_init(struct wf_jbuf *b, char *p, size_t size);
void wf_jb_raw(struct wf_jbuf *b, const char *s);
/** Zeichenkette mit Anfuehrungszeichen und Maskierung */
void wf_jb_str(struct wf_jbuf *b, const char *s);
void wf_jb_int(struct wf_jbuf *b, int64_t v);
/** "name": (mit Komma davor, wenn @p first false ist) */
void wf_jb_key(struct wf_jbuf *b, const char *name, bool first);

enum wf_jtype {
	WF_J_NUM = 0,
	WF_J_STR,
	WF_J_BOOL,
	WF_J_NULL,
};

struct wf_jval {
	enum wf_jtype type;
	int64_t num;        /**< Zahl bzw. Wahrheitswert (0/1); nur ganze Zahlen */
	const char *str;    /**< Text ohne Anfuehrungszeichen, unmaskiert in @p strbuf */
	size_t str_len;
};

/** Rueckruf je Eintrag; Rueckgabe != 0 bricht ab und wird zurueckgegeben. */
typedef int (*wf_json_visit_t)(void *ctx, const char *key, const struct wf_jval *v);

/**
 * Flaches Objekt lesen. Schluessel und Texte werden in @p strbuf entpackt.
 * @return 0, Rueckgabe des Rueckrufs oder -1 bei Syntaxfehler/Puffer zu klein
 */
int wf_json_object(const char *s, size_t len, char *strbuf, size_t strbuf_size,
		   wf_json_visit_t fn, void *ctx);

/** Ganze Zahl als einziger Inhalt (z. B. WARP-Kurzform "16000"). @return 0 oder -1 */
int wf_json_number(const char *s, size_t len, int64_t *out);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_JSON_H_ */
