/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Simulierter Flash fuer Tests: eine Loescheinheit, Programmieren kann Bits
 * nur von 1 auf 0 setzen, Stromausfall nach einer festgelegten Zahl von
 * Byte-Programmierungen bzw. waehrend des Loeschens.
 */
#ifndef FLASH_SIM_H_
#define FLASH_SIM_H_

#include <wallfree/nvm/nvm.h>

#define FSIM_MAX 16384u

struct fsim {
	struct nvm_flash fl;
	uint8_t mem[FSIM_MAX];
	int32_t budget;        /**< noch erlaubte Byte-Programmierungen, < 0 = unbegrenzt */
	bool cut_in_erase;     /**< Stromausfall beim naechsten Loeschen */
	bool dead;             /**< nach Stromausfall: alle Zugriffe scheitern */
	uint32_t writes;       /**< Schreibaufrufe */
	uint32_t bytes;        /**< programmierte Bytes */
	uint32_t erases;
	bool overprogram;      /**< Programmieren eines nicht geloeschten Bytes beobachtet */
};

void fsim_init(struct fsim *s, uint32_t size);
/** Neustart nach Stromausfall: Inhalt bleibt, Zaehler und Ausfall zurueck. */
void fsim_power_on(struct fsim *s);

#endif
