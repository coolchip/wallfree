/* SPDX-License-Identifier: Apache-2.0 */
/* Energiezaehler, siehe include/wallfree/meter/meter.h */
#include <string.h>
#include "meter_model.h"

/*
 * ABB B23 (EQ-Energiezaehler B-Serie, Kapitel Modbus): Holding-Register.
 * Bild: 0x5B00 x 28 (Index 0..27), 0x5000 x 8 (28..35), 0x5B2C x 1 (36).
 */
static const struct mblock abb_blk[] = {{3, 0x5B00, 28}, {3, 0x5000, 8}, {3, 0x5B2C, 1}};
static const struct mval abb_val[] = {
	{METER_U1, 0, VT_U32, 100},  {METER_U2, 2, VT_U32, 100},  {METER_U3, 4, VT_U32, 100},
	{METER_I1, 12, VT_U32, 10},  {METER_I2, 14, VT_U32, 10},  {METER_I3, 16, VT_U32, 10},
	{METER_P, 20, VT_S32, 10},   {METER_P1, 22, VT_S32, 10},  {METER_P2, 24, VT_S32, 10},
	{METER_P3, 26, VT_S32, 10},  {METER_E_IMP, 28, VT_U64, 10}, {METER_E_EXP, 32, VT_U64, 10},
	{METER_F, 36, VT_U16, 10},
};

/*
 * Eastron SDM630 und SDM72D-M V2 (SDM630 Modbus Protocol V1.8): Input-Register,
 * Float32. Bild: 0x0000 x 18 (0..17), 0x0034 x 24 (18..41).
 */
static const struct mblock sdm_blk[] = {{4, 0x0000, 18}, {4, 0x0034, 24}};
static const struct mval sdm_val[] = {
	{METER_U1, 0, VT_F32, 1000},  {METER_U2, 2, VT_F32, 1000},  {METER_U3, 4, VT_F32, 1000},
	{METER_I1, 6, VT_F32, 1000},  {METER_I2, 8, VT_F32, 1000},  {METER_I3, 10, VT_F32, 1000},
	{METER_P1, 12, VT_F32, 1000}, {METER_P2, 14, VT_F32, 1000}, {METER_P3, 16, VT_F32, 1000},
	{METER_P, 18, VT_F32, 1000},  {METER_F, 36, VT_F32, 1000},
	{METER_E_IMP, 38, VT_F32, 1000}, {METER_E_EXP, 40, VT_F32, 1000},
};

/* SDM72D-M V1 (SDM72D-M Modbus Protocol V1.1): nur Summenwerte. Bild: 0x0034 x 24. */
static const struct mblock sdm72v1_blk[] = {{4, 0x0034, 24}};
static const struct mval sdm72v1_val[] = {
	{METER_P, 0, VT_F32, 1000},
	{METER_E_IMP, 20, VT_F32, 1000},
	{METER_E_EXP, 22, VT_F32, 1000},
};

#define MODEL(b, v) {b, sizeof(b) / sizeof(b[0]), v, sizeof(v) / sizeof(v[0])}

static const struct meter_model models[METER_TYPE_COUNT] = {
	[METER_ABB_B23] = MODEL(abb_blk, abb_val),
	[METER_SDM630] = MODEL(sdm_blk, sdm_val),
	[METER_SDM72_V2] = MODEL(sdm_blk, sdm_val),
	[METER_SDM72_V1] = MODEL(sdm72v1_blk, sdm72v1_val),
};

const struct meter_model *meter_model_of(uint8_t type)
{
	return type > METER_NONE && type < METER_TYPE_COUNT ? &models[type] : NULL;
}

/* SOLL-MTR-13, in Tausendsteln */
static const struct {
	int64_t min;
	int64_t max;
} limits[METER_Q_COUNT] = {
	[METER_U1] = {0, 300000},           [METER_U2] = {0, 300000},
	[METER_U3] = {0, 300000},           [METER_I1] = {0, 100000},
	[METER_I2] = {0, 100000},           [METER_I3] = {0, 100000},
	[METER_P1] = {-25000000, 25000000}, [METER_P2] = {-25000000, 25000000},
	[METER_P3] = {-25000000, 25000000}, [METER_P] = {-75000000, 75000000},
	[METER_F] = {45000, 65000},         [METER_E_IMP] = {0, 100000000000LL},
	[METER_E_EXP] = {0, 100000000000LL},
};

/* WARP meter_value_id (Tinkerforge meter_value_id.csv) */
static const uint16_t warp_ids[METER_Q_COUNT] = {1, 2, 3, 13, 17, 21, 39, 48, 57, 74, 364,
						  209, 211};

void meter_init(struct meter *m, uint8_t type, uint8_t unit, const struct meter_bus *bus)
{
	memset(m, 0, sizeof(*m));
	m->model = meter_model_of(type);
	m->type = m->model != NULL ? type : METER_NONE;
	m->unit = unit;
	if (bus != NULL) {
		m->bus = *bus;
	}
	m->status = m->model != NULL ? METER_CONNECTING : METER_OFF;
}

static uint32_t u32_at(const uint16_t *r)
{
	return ((uint32_t)r[0] << 16) | r[1];
}

/*
 * IEEE-754-Float32 mal @p mul, gerundet, ohne Gleitkomma-Arithmetik: auf dem
 * Cortex-M3 zoege die Umrechnung float -> int64 die Double-Routinen nach
 * (rund 3,5 KB). Wert = Mantisse * 2^(Exponent - 150).
 * @return false fuer NaN, unendlich und Betraege ab etwa 10^13
 */
static bool f32_scaled(uint32_t u, uint16_t mul, int64_t *out)
{
	int e = (int)((u >> 23) & 0xFF);
	uint64_t x = (u & 0x7FFFFFu) | 0x800000u;
	int sh = e - 150;

	if (e == 0xFF) {
		return false;
	}
	if (e == 0) {
		*out = 0; /* Null und subnormale Zahlen (< 10^-37) */
		return true;
	}
	x *= mul; /* < 2^40 */
	if (sh > 20) {
		return false;
	}
	if (sh >= 0) {
		x <<= sh;
	} else if (sh > -48) {
		x = (x + (1ULL << (-sh - 1))) >> -sh;
	} else {
		x = 0;
	}
	*out = (u & 0x80000000u) ? -(int64_t)x : (int64_t)x;
	return true;
}

/* Ergebnis je Wert (SOLL-MTR-13) */
enum dec { DEC_OK, DEC_MISSING, DEC_BAD };

/* Ungueltig-Kennung des Zaehlers -> DEC_MISSING (nur dieser Wert fehlt);
 * NaN/unendlich oder ausserhalb der Grenzen -> DEC_BAD (Satz verwerfen) */
static enum dec decode(const struct mval *v, const uint16_t *regs, int64_t *out)
{
	const uint16_t *r = regs + v->idx;
	int64_t x;

	switch (v->type) {
	case VT_F32:
		if (!f32_scaled(u32_at(r), v->mul, &x)) {
			return DEC_BAD;
		}
		break;
	case VT_U16:
		if (r[0] == 0xFFFF) {
			return DEC_MISSING;
		}
		x = (int64_t)r[0] * v->mul;
		break;
	case VT_U32:
		if (u32_at(r) == 0xFFFFFFFFu) {
			return DEC_MISSING;
		}
		x = (int64_t)u32_at(r) * v->mul;
		break;
	case VT_S32:
		if (u32_at(r) == 0x7FFFFFFFu) {
			return DEC_MISSING;
		}
		x = (int64_t)(int32_t)u32_at(r) * v->mul;
		break;
	default: { /* VT_U64 */
		uint64_t u = ((uint64_t)u32_at(r) << 32) | u32_at(r + 2);

		/* Ungueltig: alle Bytes FF; groesser als die Grenze faengt der Bereich ab */
		if (u == UINT64_MAX) {
			return DEC_MISSING;
		}
		if (u > (uint64_t)INT64_MAX / v->mul) {
			return DEC_BAD;
		}
		x = (int64_t)u * v->mul;
		break;
	}
	}
	if (x < limits[v->q].min || x > limits[v->q].max) {
		return DEC_BAD;
	}
	*out = x;
	return DEC_OK;
}

static void set_status(struct meter *m, uint8_t st)
{
	if (m->status != st) {
		m->status = st;
		m->events |= METER_EV_STATUS;
	}
}

/* SOLL-MTR-10/11: bis zu METER_TRIES Versuche, nach Timeout Zusatzpause */
static bool read_block(struct meter *m, const struct mblock *b, uint16_t *out)
{
	for (int i = 0; i < METER_TRIES; i++) {
		uint32_t gap = METER_GAP_MS + (m->after_timeout ? METER_RESPONSE_MS : 0);

		m->bus.sleep_ms(m->bus.ctx, gap);
		m->after_timeout = false;
		m->st.requests++;
		int r = m->bus.read(m->bus.ctx, m->unit, b->fc, b->reg, b->n, out);

		if (r == METER_BUS_OK) {
			m->st.responses++;
			return true;
		}
		if (r == METER_BUS_TIMEOUT) {
			m->st.timeouts++;
			m->after_timeout = true;
		} else if (r > 0) {
			m->st.exceptions++;
			m->st.last_exception = (uint8_t)r;
		} else {
			m->st.frame_errors++;
		}
	}
	return false;
}

uint32_t meter_cycle(struct meter *m)
{
	const struct meter_model *md = m->model;
	uint16_t regs[REGS_MAX];
	int64_t v[METER_Q_COUNT];
	bool have[METER_Q_COUNT] = {false};
	uint16_t pos = 0;
	bool ok = true;

	if (md == NULL) {
		return METER_PERIOD_ERROR_MS;
	}
	/* SOLL-MTR-12: alle Bloecke desselben Zyklus */
	for (uint8_t i = 0; i < md->nblk && ok; i++) {
		ok = read_block(m, &md->blk[i], regs + pos);
		pos += md->blk[i].n;
	}
	for (uint8_t i = 0; i < md->nval && ok; i++) {
		enum dec d = decode(&md->val[i], regs, &v[md->val[i].q]);

		if (d == DEC_BAD) {
			m->st.implausible++;
			ok = false;
		}
		have[md->val[i].q] = d == DEC_OK;
	}
	if (ok) {
		/* SOLL-MTR-13: Ruecksprung uebernehmen, aber melden */
		if (m->have_set && ((have[METER_E_IMP] && m->have[METER_E_IMP] &&
				     v[METER_E_IMP] < m->v[METER_E_IMP]) ||
				    (have[METER_E_EXP] && m->have[METER_E_EXP] &&
				     v[METER_E_EXP] < m->v[METER_E_EXP]))) {
			m->events |= METER_EV_ENERGY_DOWN;
		}
		memcpy(m->v, v, sizeof(v));
		memcpy(m->have, have, sizeof(have));
		m->have_set = true;
		m->t_set = m->bus.now_ms(m->bus.ctx);
		m->st.sets++;
		m->fails = 0;
		set_status(m, METER_OK);
	} else if (m->fails < METER_FAILS_ERROR && ++m->fails == METER_FAILS_ERROR) {
		set_status(m, METER_ERROR);
	}
	return m->status == METER_ERROR ? METER_PERIOD_ERROR_MS : METER_PERIOD_MS;
}

bool meter_has(uint8_t type, enum meter_q q)
{
	const struct meter_model *md = meter_model_of(type);

	for (uint8_t i = 0; md != NULL && i < md->nval; i++) {
		if (md->val[i].q == q) {
			return true;
		}
	}
	return false;
}

void meter_values(const struct meter *m, int64_t now, struct meter_values *out)
{
	bool fresh = m->have_set && now - m->t_set <= METER_STALE_MS;

	memset(out, 0, sizeof(*out));
	out->type = m->type;
	out->status = m->status;
	out->age_ms = m->have_set ? now - m->t_set : -1;
	out->st = m->st;
	for (int q = 0; q < METER_Q_COUNT; q++) {
		out->valid[q] = fresh && meter_has(m->type, (enum meter_q)q) && m->have[q];
		out->v[q] = out->valid[q] ? m->v[q] : 0;
	}
}

uint8_t meter_take_events(struct meter *m)
{
	uint8_t e = m->events;

	m->events = 0;
	return e;
}

uint16_t meter_warp_id(enum meter_q q)
{
	return q < METER_Q_COUNT ? warp_ids[q] : 0;
}

const char *meter_status_name(uint8_t status)
{
	static const char *const n[] = {"off", "connecting", "ok", "error"};

	return status < 4 ? n[status] : "?";
}

const char *meter_type_name(uint8_t type)
{
	static const char *const n[METER_TYPE_COUNT] = {"kein Zaehler", "ABB B23",
							"Eastron SDM630", "Eastron SDM72D-M V2",
							"Eastron SDM72D-M (V1)"};

	return type < METER_TYPE_COUNT ? n[type] : "?";
}

uint16_t meter_regs_total(uint8_t type)
{
	const struct meter_model *md = meter_model_of(type);
	uint16_t n = 0;

	for (uint8_t i = 0; md != NULL && i < md->nblk; i++) {
		n += md->blk[i].n;
	}
	return n;
}
