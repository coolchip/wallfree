/* SPDX-License-Identifier: Apache-2.0 */
/* Modelltabellen, intern fuer meter.c und meter_sim.c */
#ifndef WALLFREE_METER_MODEL_H_
#define WALLFREE_METER_MODEL_H_

#include <wallfree/meter/meter.h>

enum vtype {
	VT_F32 = 0, /* IEEE 754, Einheit der Messgroesse */
	VT_U16,
	VT_U32,
	VT_S32,
	VT_U64,
};

struct mblock {
	uint8_t fc;
	uint16_t reg;
	uint8_t n;
};

struct mval {
	uint8_t q;      /* enum meter_q */
	uint8_t idx;    /* Register im Gesamtbild (Bloecke hintereinander) */
	uint8_t type;   /* enum vtype */
	uint16_t mul;   /* Faktor auf Tausendstel der Einheit */
};

struct meter_model {
	const struct mblock *blk;
	uint8_t nblk;
	const struct mval *val;
	uint8_t nval;
};

#define REGS_MAX 48

/** @return NULL fuer METER_NONE und unbekannte Typen */
const struct meter_model *meter_model_of(uint8_t type);

#endif
