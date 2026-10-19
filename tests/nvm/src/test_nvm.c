/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Parameterspeicher, Abschnitt 13.1, SOLL-NVM-01..05 */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/nvm/nvm.h>
#include "flash_sim.h"

#define AREA 2048u
#define ENT4 12u /* Eintrag mit 4-Byte-Wert */

static struct fsim fs;
static struct nvm n;
static uint8_t scratch[1024];

static void before(void *f)
{
	ARG_UNUSED(f);
	fsim_init(&fs, AREA);
}

ZTEST_SUITE(nvm, NULL, NULL, before, NULL, NULL);

static int put(uint16_t id, int32_t v)
{
	return nvm_write(&n, id, &v, sizeof(v));
}

#define NONE INT32_MIN
static int32_t get(uint16_t id)
{
	int32_t v;
	int r = nvm_read(&n, id, &v, sizeof(v));

	return r == (int)sizeof(v) ? v : NONE;
}

static void remount(void)
{
	fsim_power_on(&fs);
	zassert_equal(nvm_mount(&n, &fs.fl), NVM_OK);
}

ZTEST(nvm, test_blank_area_is_empty)
{
	remount();
	zassert_equal(n.state, NVM_ST_EMPTY);
	zassert_equal(get(1), NONE);
	zassert_false(nvm_needs_compact(&n, 0));
	zassert_equal(nvm_free(&n), AREA - NVM_HDR_SIZE);

	/* erster Wert legt den Kopf ohne Loeschen an */
	zassert_equal(put(1, 42), NVM_OK);
	zassert_equal(n.state, NVM_ST_OK);
	zassert_equal(fs.erases, 0);
	remount();
	zassert_equal(n.state, NVM_ST_OK);
	zassert_equal(get(1), 42);
	zassert_equal(nvm_used(&n), NVM_HDR_SIZE + ENT4);
	zassert_false(fs.overprogram);
}

ZTEST(nvm, test_latest_wins_no_rewrite)
{
	remount();
	zassert_equal(put(7, 1), NVM_OK);
	zassert_equal(put(8, -5), NVM_OK);
	zassert_equal(put(7, 2), NVM_OK);
	zassert_equal(put(7, 3), NVM_OK);
	zassert_equal(get(7), 3);
	zassert_equal(get(8), -5);

	/* unveraenderter Wert: kein Schreibvorgang */
	uint32_t b = fs.bytes;

	zassert_equal(put(7, 3), NVM_OK);
	zassert_equal(fs.bytes, b);

	remount();
	zassert_equal(get(7), 3);
	zassert_equal(get(8), -5);
	zassert_equal(nvm_used(&n), NVM_HDR_SIZE + 4 * ENT4);
}

ZTEST(nvm, test_lengths_and_ids)
{
	uint8_t big[NVM_MAX_LEN + 1];
	uint8_t out[NVM_MAX_LEN];

	memset(big, 0xA5, sizeof(big));
	remount();
	zassert_equal(nvm_write(&n, 1, big, 0), NVM_E_ARG);
	zassert_equal(nvm_write(&n, 1, big, NVM_MAX_LEN + 1), NVM_E_ARG);
	zassert_equal(nvm_write(&n, NVM_ID_INVALID, big, 4), NVM_E_ARG);
	zassert_equal(nvm_write(&n, 1, NULL, 4), NVM_E_ARG);
	zassert_equal(nvm_write(&n, 1, big, NVM_MAX_LEN), NVM_OK);
	zassert_equal(nvm_write(&n, 2, "wallfree", 8), NVM_OK);
	zassert_equal(nvm_write(&n, 3, "x", 1), NVM_OK);
	/* Laengenwechsel derselben Kennung */
	zassert_equal(nvm_write(&n, 2, "wf", 2), NVM_OK);

	remount();
	zassert_equal(nvm_read(&n, 1, out, sizeof(out)), NVM_MAX_LEN);
	zassert_mem_equal(out, big, NVM_MAX_LEN);
	zassert_equal(nvm_read(&n, 1, out, 10), NVM_E_ARG, "Puffer zu klein");
	zassert_equal(nvm_read(&n, 2, out, sizeof(out)), 2);
	zassert_mem_equal(out, "wf", 2);
	zassert_equal(nvm_read(&n, 3, out, sizeof(out)), 1);
	zassert_equal(nvm_read(&n, 4, out, sizeof(out)), NVM_E_NOENT);
}

ZTEST(nvm, test_unmounted)
{
	struct nvm u = {0};
	int32_t v = 1;

	zassert_equal(nvm_write(&u, 1, &v, 4), NVM_E_STATE);
	zassert_equal(nvm_read(&u, 1, &v, 4), NVM_E_STATE);
	zassert_equal(nvm_compact(&u, scratch, sizeof(scratch)), NVM_E_STATE);
	zassert_equal(nvm_format(&u), NVM_E_STATE);
}

static void fill_until_pending(uint16_t id)
{
	int32_t v = 1000;

	for (;;) {
		int r = put(id, v++);

		if (r == NVM_PENDING) {
			break;
		}
		zassert_equal(r, NVM_OK);
	}
}

ZTEST(nvm, test_full_goes_pending_then_compact)
{
	remount();
	zassert_equal(put(3, 333), NVM_OK);
	fill_until_pending(1);
	int32_t last = get(1);

	/* SOLL-NVM-03/04: im Betrieb nie loeschen; Wert gilt sofort aus dem RAM */
	zassert_equal(fs.erases, 0);
	zassert_equal(n.npend, 1);
	zassert_true(nvm_needs_compact(&n, 0));
	zassert_equal(put(2, 222), NVM_PENDING);
	zassert_equal(put(1, last + 1), NVM_PENDING, "bleibt im RAM, nicht hinter den Puffer");
	zassert_equal(n.npend, 2);
	zassert_equal(get(1), last + 1);
	zassert_equal(nvm_free(&n) < ENT4, true);

	zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_OK);
	zassert_equal(fs.erases, 1);
	zassert_equal(n.npend, 0);
	zassert_false(nvm_needs_compact(&n, 0));
	zassert_equal(nvm_used(&n), NVM_HDR_SIZE + 3 * ENT4);

	remount();
	zassert_equal(get(1), last + 1);
	zassert_equal(get(2), 222);
	zassert_equal(get(3), 333);
	zassert_false(fs.overprogram);
}

ZTEST(nvm, test_reserve_triggers_compact)
{
	remount();
	zassert_equal(put(1, 1), NVM_OK);
	zassert_false(nvm_needs_compact(&n, 256));
	zassert_true(nvm_needs_compact(&n, AREA));
}

ZTEST(nvm, test_pending_overflow)
{
	remount();
	fill_until_pending(1);
	for (uint16_t id = 2; id <= NVM_PENDING_MAX; id++) {
		zassert_equal(put(id, id), NVM_PENDING);
	}
	zassert_equal(put(100, 1), NVM_E_FULL);
	zassert_equal(get(100), NONE);
	/* vorhandene Kennung im Puffer bleibt aenderbar */
	zassert_equal(put(5, 55), NVM_PENDING);
	zassert_equal(get(5), 55);
}

ZTEST(nvm, test_compact_scratch_too_small)
{
	remount();
	for (uint16_t id = 1; id <= 20; id++) {
		zassert_equal(put(id, id * 10), NVM_OK);
	}
	zassert_equal(nvm_compact(&n, scratch, 64), NVM_E_NOSPACE);
	zassert_equal(fs.erases, 0, "nichts geloescht");
	remount();
	zassert_equal(get(20), 200);
}

ZTEST(nvm, test_format)
{
	remount();
	zassert_equal(put(1, 1), NVM_OK);
	fill_until_pending(2);
	zassert_equal(nvm_format(&n), NVM_OK);
	zassert_equal(n.npend, 0);
	zassert_equal(get(1), NONE);
	remount();
	zassert_equal(n.state, NVM_ST_OK);
	zassert_equal(get(1), NONE);
	zassert_equal(get(2), NONE);
	zassert_equal(nvm_used(&n), NVM_HDR_SIZE);
}

ZTEST(nvm, test_crc_error_ignored)
{
	remount();
	zassert_equal(put(1, 1), NVM_OK);
	zassert_equal(put(1, 2), NVM_OK);
	zassert_equal(put(2, 5), NVM_OK);
	/* Datenbit im zweiten Eintrag kippen */
	fs.mem[NVM_HDR_SIZE + ENT4 + 4] ^= 0x01;
	remount();
	zassert_equal(get(1), 1, "aelterer gueltiger Wert gilt");
	zassert_equal(get(2), 5, "nachfolgende Eintraege bleiben lesbar");
	zassert_false(n.damaged);
}

static int visits;
static void count_visit(void *ctx, uint16_t id, const void *data, size_t len)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(id);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	visits++;
}

ZTEST(nvm, test_foreach_order_and_pending)
{
	remount();
	zassert_equal(put(1, 1), NVM_OK);
	zassert_equal(put(1, 2), NVM_OK);
	fill_until_pending(3);
	visits = 0;
	zassert_equal(nvm_foreach(&n, count_visit, NULL), NVM_OK);
	zassert_equal(visits, (int)((nvm_used(&n) - NVM_HDR_SIZE) / ENT4) + 1);
}

ZTEST(nvm, test_foreign_or_newer_version_invalid)
{
	fs.mem[0] = 0x12; /* fremder Inhalt */
	remount();
	zassert_equal(n.state, NVM_ST_INVALID);
	zassert_equal(nvm_free(&n), 0);
	zassert_true(nvm_needs_compact(&n, 0));
	/* Werte bleiben im RAM, bis verdichtet wird */
	zassert_equal(put(1, 9), NVM_PENDING);
	zassert_equal(get(1), 9);
	zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_OK);
	remount();
	zassert_equal(n.state, NVM_ST_OK);
	zassert_equal(get(1), 9);

	/* andere Formatversion */
	fs.mem[4] = 0x02;
	remount();
	zassert_equal(n.state, NVM_ST_INVALID);
	zassert_equal(get(1), NONE);
}

/* Stromausfall nach jedem Byte eines Anhaengevorgangs (SOLL-NVM-02) */
ZTEST(nvm, test_power_cut_during_append)
{
	static uint8_t base[AREA];

	remount();
	zassert_equal(put(1, 100), NVM_OK);
	zassert_equal(put(2, 200), NVM_OK);
	memcpy(base, fs.mem, AREA);

	for (int32_t cut = 0; cut <= (int32_t)ENT4; cut++) {
		memcpy(fs.mem, base, AREA);
		remount();
		fs.budget = cut;
		int r = put(1, 111);

		zassert_true(cut == ENT4 ? r == NVM_OK : r == NVM_E_IO, "cut %d r %d", cut, r);
		remount();
		int32_t v = get(1);

		zassert_true(v == 100 || v == 111, "cut %d: %d", cut, v);
		zassert_equal(v == 111, cut == ENT4, "cut %d", cut);
		zassert_equal(get(2), 200);

		/* danach weiterarbeiten: anhaengen oder RAM + Verdichten */
		r = put(3, 333);
		zassert_true(r == NVM_OK || r == NVM_PENDING, "cut %d r %d", cut, r);
		if (nvm_needs_compact(&n, 0)) {
			zassert_true(cut > 0 && cut < 4, "nur ein zerstoerter Kopf sperrt den Rest");
			zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_OK);
		}
		remount();
		zassert_false(n.damaged);
		zassert_equal(get(3), 333, "cut %d", cut);
		zassert_equal(get(2), 200);
		zassert_false(fs.overprogram, "cut %d", cut);
	}
}

/* Stromausfall an jeder Stelle des Verdichtens (SOLL-NVM-05) */
ZTEST(nvm, test_power_cut_during_compact)
{
	static uint8_t base[AREA];
	struct nvm saved;
	uint32_t total;

	remount();
	zassert_equal(put(5, 55), NVM_OK);
	fill_until_pending(1);
	zassert_equal(put(2, 22), NVM_PENDING);
	int32_t v1 = get(1);

	memcpy(base, fs.mem, AREA);
	saved = n;

	/* Gesamtzahl der Bytes eines ungestoerten Verdichtens */
	fs.bytes = 0;
	zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_OK);
	total = fs.bytes;
	zassert_equal(total, NVM_HDR_SIZE + 3 * ENT4);

	/* Ausfall waehrend des Loeschens */
	memcpy(fs.mem, base, AREA);
	fsim_power_on(&fs);
	n = saved;
	fs.cut_in_erase = true;
	zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_E_IO);
	zassert_equal(n.state, NVM_ST_INVALID);
	zassert_equal(get(2), 22, "RAM-Werte bleiben bis zum Neustart");
	remount();
	zassert_equal(n.state, NVM_ST_INVALID, "-> Werkseinstellungen und Meldung");

	for (int32_t cut = 0; cut <= (int32_t)total; cut++) {
		memcpy(fs.mem, base, AREA);
		fsim_power_on(&fs);
		n = saved;
		fs.budget = cut;
		int r = nvm_compact(&n, scratch, sizeof(scratch));

		zassert_equal(r, cut == (int32_t)total ? NVM_OK : NVM_E_IO, "cut %d", cut);
		remount();
		if (cut == 0) {
			/* einziges Fenster: geloescht, noch nichts geschrieben */
			zassert_equal(n.state, NVM_ST_EMPTY);
		} else if (n.state == NVM_ST_INVALID) {
			/* das letzte Abschlussbyte ist 0xFF und braucht keine Programmierung */
			zassert_true(cut < (int32_t)total - 1, "cut %d", cut);
			zassert_equal(get(5), NONE, "keine Teilergebnisse");
		} else {
			zassert_true(cut >= (int32_t)total - 1, "cut %d", cut);
			zassert_equal(n.state, NVM_ST_OK);
			zassert_equal(get(1), v1);
			zassert_equal(get(2), 22);
			zassert_equal(get(5), 55);
		}
		zassert_false(fs.overprogram, "cut %d", cut);
	}
}

/* Ein Bit im Kopf eines Eintrags gekippt: Rest unbrauchbar, Werte davor bleiben */
ZTEST(nvm, test_damaged_header)
{
	remount();
	zassert_equal(put(1, 1), NVM_OK);
	zassert_equal(put(2, 2), NVM_OK);
	zassert_equal(put(3, 3), NVM_OK);
	fs.mem[NVM_HDR_SIZE + ENT4 + 2] ^= 0x01; /* Laenge des zweiten Eintrags */
	remount();
	zassert_true(n.damaged);
	zassert_equal(nvm_free(&n), 0);
	zassert_equal(get(1), 1);
	zassert_equal(get(2), NONE);
	zassert_equal(get(3), NONE);
	zassert_equal(put(4, 4), NVM_PENDING);
	zassert_equal(nvm_compact(&n, scratch, sizeof(scratch)), NVM_OK);
	remount();
	zassert_false(n.damaged);
	zassert_equal(get(1), 1);
	zassert_equal(get(4), 4);
}

ZTEST(nvm, test_crc_reference_values)
{
	/* Pruefwerte: CRC-32/IEEE("123456789") = 0xCBF43926, CRC-8/SMBUS = 0xF4 */
	zassert_equal(nvm_crc32(0, (const uint8_t *)"123456789", 9), 0xCBF43926u);
	zassert_equal(nvm_crc8((const uint8_t *)"123456789", 9), 0xF4);
	/* inkrementell */
	uint32_t c = nvm_crc32(0, (const uint8_t *)"1234", 4);

	zassert_equal(nvm_crc32(c, (const uint8_t *)"56789", 5), 0xCBF43926u);
}
