/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Nur-Lese-Zugriff auf den Parameterbereich der Originalfirmware (Sektor 3,
 * SOLL-NVM-10). Format nach wallbe2/docs/parameterspeicher.md (eigene
 * Beschreibung des Befunds):
 *
 *   Offset 0: Halbwort 0x0000 = Bereich gueltig, sonst ungueltig
 *   ab Offset 4, je 4 Byte: Halbwort Wert, Halbwort Kennung; frei = 0xFFFFFFFF
 *
 * Kennung = Feldnummer (+ Halbwortindex bei Feldern ueber 2 Byte); 1-Byte-
 * Felder stehen im unteren Byte des Halbworts. Der juengste Eintrag gilt.
 * Es wird nie geschrieben oder geloescht.
 */
#ifndef WALLFREE_NVM_ORIG_H_
#define WALLFREE_NVM_ORIG_H_

#include <wallfree/nvm/nvm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Juengsten Wert einer Kennung suchen (nutzt nur fl->read).
 * @return NVM_OK, NVM_E_NOENT, NVM_E_STATE (Bereich ungueltig) oder NVM_E_IO
 */
int nvm_orig_read(const struct nvm_flash *fl, uint16_t key, uint16_t *value);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_NVM_ORIG_H_ */
