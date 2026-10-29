/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_SHELL_WEB_H_
#define WALLFREE_APP_SHELL_WEB_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct wf_jbuf;

#define SHELL_WEB_DONE SIZE_MAX

/**
 * Eine Kommandozeile der Shell ausfuehren (Konsole der Weboberflaeche, SOLL-SH-19), im
 * aufrufenden Thread. Die Ausgabe landet NUL-terminiert in @p buf.
 * @param cut true, wenn die Ausgabe nicht in @p buf passte und gekuerzt ist
 * @return Laenge der Ausgabe; -ENOMEM Zeile zu lang, -ENOEXEC Shell belegt
 */
int shell_web_exec(const char *line, char *buf, size_t size, bool *cut);

/**
 * Kommandobaum als JSON fuer die Vervollstaendigung (SOLL-SH-23): [["evse","Ladelogik"],
 * ["evse status","..."], ...], in Teilen. Erster Aufruf mit 0, dann mit dem Rueckgabewert,
 * bis SHELL_WEB_DONE.
 */
size_t shell_web_commands_json(size_t cursor, struct wf_jbuf *b);

#endif
