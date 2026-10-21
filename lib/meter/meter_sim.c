/* SPDX-License-Identifier: Apache-2.0 */
/* Simulierter Zaehler fuer PC-Tests und native_sim, siehe meter_sim_encode() */
#include <string.h>
#include "meter_model.h"

static void put32(uint16_t *r, uint32_t u)
{
	r[0] = (uint16_t)(u >> 16);
	r[1] = (uint16_t)u;
}

static void encode(const struct mval *v, int64_t x, bool have, uint16_t *img)
{
	uint16_t *r = img + v->idx;

	switch (v->type) {
	case VT_F32: {
		float f = have ? (float)x / (float)v->mul : __builtin_nanf("");
		uint32_t u;

		memcpy(&u, &f, sizeof(u));
		put32(r, u);
		break;
	}
	case VT_U16:
		r[0] = have ? (uint16_t)(x / v->mul) : 0xFFFF;
		break;
	case VT_U32:
		put32(r, have ? (uint32_t)(x / v->mul) : 0xFFFFFFFFu);
		break;
	case VT_S32:
		put32(r, have ? (uint32_t)(int32_t)(x / v->mul) : 0x7FFFFFFFu);
		break;
	default: { /* VT_U64 */
		uint64_t u = have ? (uint64_t)(x / v->mul) : UINT64_MAX;

		put32(r, (uint32_t)(u >> 32));
		put32(r + 2, (uint32_t)u);
		break;
	}
	}
}

int meter_sim_encode(uint8_t type, uint8_t fc, uint16_t reg, uint16_t n, const int64_t *v,
		     uint16_t *out)
{
	const struct meter_model *md = meter_model_of(type);
	uint16_t img[REGS_MAX] = {0};
	uint16_t pos = 0;

	if (md == NULL) {
		return 2;
	}
	for (uint8_t i = 0; i < md->nval; i++) {
		encode(&md->val[i], v[md->val[i].q], true, img);
	}
	for (uint8_t i = 0; i < md->nblk; i++) {
		const struct mblock *b = &md->blk[i];

		if (b->fc == fc && b->reg == reg && b->n == n) {
			memcpy(out, img + pos, n * sizeof(uint16_t));
			return METER_BUS_OK;
		}
		pos += b->n;
	}
	return 2; /* Illegal data address */
}
