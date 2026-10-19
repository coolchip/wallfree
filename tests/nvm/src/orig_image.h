/* SPDX-License-Identifier: Apache-2.0 */
/* Synthetisches Abbild des Originalbereichs (Sektor 3) fuer Tests */
#ifndef ORIG_IMAGE_H_
#define ORIG_IMAGE_H_

#include <stdint.h>
#include <string.h>

/** Gueltigen, leeren Bereich anlegen (16 KB). */
static inline void orig_image_init(uint8_t *mem)
{
	memset(mem, 0xFF, 16384);
	mem[0] = 0;
	mem[1] = 0;
}

/** Eintrag an die erste freie Stelle anhaengen. @return Offset */
static inline uint32_t orig_image_add(uint8_t *mem, uint16_t key, uint16_t value)
{
	uint32_t off = 4;

	while (mem[off] != 0xFF || mem[off + 1] != 0xFF || mem[off + 2] != 0xFF ||
	       mem[off + 3] != 0xFF) {
		off += 4;
	}
	mem[off] = (uint8_t)value;
	mem[off + 1] = (uint8_t)(value >> 8);
	mem[off + 2] = (uint8_t)key;
	mem[off + 3] = (uint8_t)(key >> 8);
	return off;
}

#endif
