/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_SHELL_WEB_H_
#define WALLFREE_APP_SHELL_WEB_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * Eine Kommandozeile der Shell ausfuehren (Konsole der Weboberflaeche, SOLL-SH-19), im
 * aufrufenden Thread. Die Ausgabe landet NUL-terminiert in @p buf.
 * @param cut true, wenn die Ausgabe nicht in @p buf passte und gekuerzt ist
 * @return Laenge der Ausgabe; -ENOMEM Zeile zu lang, -ENOEXEC Shell belegt
 */
int shell_web_exec(const char *line, char *buf, size_t size, bool *cut);

#endif
