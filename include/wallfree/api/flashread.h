/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Lesen von Flash, OTP und Optionsbytes ueber die Konsole (SOLL-SH-24): erlaubte Bereiche und
 * Base64-Ausgabe. Reines C, auf dem PC testbar.
 */
#ifndef WALLFREE_API_FLASHREAD_H_
#define WALLFREE_API_FLASHREAD_H_

#include <stddef.h>
#include <stdint.h>

/** Hoechstens so viele Byte je Aufruf (Ausgabe passt in den Konsolenpuffer von 2 KB). */
#define WF_FLASHREAD_MAX 1024u

/**
 * Prueft, ob [addr, addr + len) ganz in einem erlaubten Bereich liegt: Sektor 0..6,
 * OTP mit Sperrbytes, Optionsbytes. Sektor 7 (Zugangsdaten) ist ausgeschlossen.
 * @return 0 erlaubt, -1 Laenge 0 oder ueber WF_FLASHREAD_MAX, -2 Bereich nicht erlaubt
 */
int wf_flashread_check(uint32_t addr, uint32_t len);

/**
 * Base64 (RFC 4648, mit Auffuellung) ohne Zeilenumbrueche.
 * @return Laenge der Ausgabe ohne Nullzeichen, 0 wenn out zu klein (out_size muss
 *         4 * ((n + 2) / 3) + 1 fassen)
 */
size_t wf_base64(const uint8_t *in, size_t n, char *out, size_t out_size);

#endif /* WALLFREE_API_FLASHREAD_H_ */
