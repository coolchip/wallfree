/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Tests Persistenz der Ladelogik: SOLL-BOOT-04, SOLL-NVM-03/04/10,
 * SOLL-PAR-03/40 (Verriegelungszustand 465).
 */
#include <zephyr/ztest.h>
#include <wallfree/evse/persist.h>
#include <wallfree/nvm/orig.h>
#include "flash_sim.h"
#include "orig_image.h"
#include "sim.h"

static struct sim s;
static struct evse_config cfg;
static struct fsim fs, fo;
static struct nvm n;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	sim_init(&s, &cfg);
	fsim_init(&fs, 4096);
	fsim_init(&fo, 16384);
	zassert_equal(nvm_mount(&n, &fs.fl), NVM_OK);
}

ZTEST_SUITE(evse_persist, NULL, NULL, before, NULL, NULL);

static int32_t stored(uint16_t id)
{
	int32_t v;

	return nvm_read(&n, id, &v, sizeof(v)) == 4 ? v : INT32_MIN;
}

ZTEST(evse_persist, test_load_empty_keeps_defaults)
{
	struct evse_config c, d;
	enum evse_lock_state lock = EVSE_LOCK_UNLOCKED;
	struct evse_load_result res;

	evse_config_defaults(&c);
	d = c;
	zassert_equal(evse_persist_load(&n, &c, &lock, &res), NVM_OK);
	zassert_mem_equal(&c, &d, sizeof(c));
	zassert_equal(lock, EVSE_LOCK_UNLOCKED);
	zassert_equal(res.restored + res.rejected + res.unknown, 0);
}

ZTEST(evse_persist, test_roundtrip)
{
	struct evse_config c;
	enum evse_lock_state lock = EVSE_LOCK_UNLOCKED;
	struct evse_load_result res;
	const struct evse_param *lp = evse_param_find("lock_pulse");
	const struct evse_param *ec = evse_param_find("external_current");
	const struct evse_param *wd = evse_param_find("wakeup_delay");

	sim_boot(&s);
	zassert_equal(evse_param_set(&s.e, lp, 1200, s.now), EVSE_PARAM_OK);
	zassert_equal(evse_persist_param(&n, lp, 1200), NVM_OK);
	zassert_equal(evse_persist_param(&n, wd, 0), NVM_OK);

	/* SOLL-NVM-03: Laufzeitwerte nie speichern */
	uint32_t b = fs.bytes;

	zassert_equal(evse_persist_param(&n, ec, 6000), NVM_OK);
	zassert_equal(fs.bytes, b);
	zassert_equal(stored(528), INT32_MIN);

	/* 465: nur Endzustaende */
	zassert_equal(evse_persist_lock(&n, EVSE_LOCK_LOCKING), NVM_OK);
	zassert_equal(stored(EVSE_NVM_ID_LOCK), INT32_MIN);
	zassert_equal(evse_persist_lock(&n, EVSE_LOCK_LOCKED), NVM_OK);
	zassert_equal(stored(EVSE_NVM_ID_LOCK), EVSE_LOCK_LOCKED);

	fsim_power_on(&fs);
	zassert_equal(nvm_mount(&n, &fs.fl), NVM_OK);
	evse_config_defaults(&c);
	zassert_equal(evse_persist_load(&n, &c, &lock, &res), NVM_OK);
	zassert_equal(c.lock_pulse_ms, 1200);
	zassert_equal(c.wakeup_delay_ms, 0);
	zassert_equal(lock, EVSE_LOCK_LOCKED);
	zassert_equal(res.restored, 3);
	zassert_equal(res.rejected, 0);

	/* geladene Konfiguration startet den Automaten mit gespeicherter Verriegelung */
	evse_init(&s.e, &c, lock, 0);
	zassert_equal(s.e.lock.state, EVSE_LOCK_LOCKED);
}

ZTEST(evse_persist, test_load_rejects_invalid)
{
	struct evse_config c;
	enum evse_lock_state lock = EVSE_LOCK_UNLOCKED;
	struct evse_load_result res;
	int32_t v;

	v = 50; /* 525 ausserhalb 100..3000 */
	zassert_equal(nvm_write(&n, 525, &v, 4), NVM_OK);
	v = 7;
	zassert_equal(nvm_write(&n, 1999, &v, 4), NVM_OK); /* unbekannt */
	zassert_equal(nvm_write(&n, 2000, &v, 4), NVM_OK); /* Statuswert */
	zassert_equal(nvm_write(&n, 3001, &v, 4), NVM_OK); /* Anwendung: nicht gezaehlt */
	zassert_equal(nvm_write(&n, 526, "ab", 2), NVM_OK); /* falsche Laenge */
	v = EVSE_LOCK_ERROR;
	zassert_equal(nvm_write(&n, EVSE_NVM_ID_LOCK, &v, 4), NVM_OK);
	v = 1; /* 409 ohne Messquelle nicht zulaessig */
	zassert_equal(nvm_write(&n, 409, &v, 4), NVM_OK);

	evse_config_defaults(&c);
	zassert_equal(evse_persist_load(&n, &c, &lock, &res), NVM_OK);
	zassert_equal(c.lock_pulse_ms, 500);
	zassert_equal(c.unlock_pulse_ms, 500);
	zassert_false(c.overcurrent_enable);
	zassert_equal(lock, EVSE_LOCK_UNLOCKED);
	zassert_equal(res.restored, 0);
	zassert_equal(res.rejected, 5);
	zassert_equal(res.unknown, 1);
}

ZTEST(evse_persist, test_erase_allowed)
{
	sim_start(&s);
	zassert_false(evse_flash_erase_allowed(&s.e), "S0: Selbsttest/Start-Entriegelung");
	sim_boot(&s);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
	zassert_true(evse_flash_erase_allowed(&s.e));

	s.car = EVSE_CP_B;
	sim_run(&s, 100);
	zassert_false(evse_flash_erase_allowed(&s.e), "Fahrzeug angeschlossen");
	s.car = EVSE_CP_A;
	sim_run(&s, 3000);
	zassert_equal(s.e.state, EVSE_S1_IDLE);
	zassert_true(evse_flash_erase_allowed(&s.e));
}

/* ------------------------------------------------ Import aus Sektor 3 */

static int counts[EVSE_IMP_NOT_SAVED + 1];
static int last_err[1000];

static void report(void *ctx, uint16_t field, const char *name, int32_t value,
		   enum evse_import_result res, int err)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(value);
	zassert_not_null(name);
	counts[res]++;
	if (field < ARRAY_SIZE(last_err)) {
		last_err[field] = err;
	}
}

ZTEST(evse_persist, test_import)
{
	memset(counts, 0, sizeof(counts));
	orig_image_init(fo.mem);
	orig_image_add(fo.mem, 525, 900);
	orig_image_add(fo.mem, 525, 1500);  /* juengster gilt */
	orig_image_add(fo.mem, 390, 0);
	orig_image_add(fo.mem, 409, 0x0001); /* Ueberstrom ohne Messquelle */
	orig_image_add(fo.mem, 412, 0x0000);
	orig_image_add(fo.mem, 520, 22);
	orig_image_add(fo.mem, 527, 50);     /* ausserhalb 500..10000 */
	orig_image_add(fo.mem, 327, 43);     /* Code > 42 */
	orig_image_add(fo.mem, 315, 192);    /* Netz: nicht Sache der Ladelogik */

	sim_boot(&s);
	int r = evse_import_orig(&s.e, &fo.fl, &n, s.now, report, NULL);

	/* nicht gespeichert = 0: 328..330, 519, 521..524 uebernommen, 526 (min 100) abgelehnt */
	zassert_equal(r, 12, "525, 390, 412, 520 und acht Nullen");
	zassert_equal(counts[EVSE_IMP_DONE], 4);
	zassert_equal(counts[EVSE_IMP_ZERO], 8);
	zassert_equal(counts[EVSE_IMP_REJECTED], 4);
	zassert_equal(last_err[409], EVSE_PARAM_E_UNAVAILABLE);
	zassert_equal(last_err[527], EVSE_PARAM_E_RANGE);
	zassert_equal(last_err[327], EVSE_PARAM_E_RANGE);
	zassert_equal(last_err[526], EVSE_PARAM_E_RANGE);
	zassert_equal(s.e.cfg.out_function[EVSE_DOUT_LR], 0, "fehlt = 0");
	zassert_equal(stored(329), 0);
	zassert_equal(s.e.cfg.unlock_pulse_ms, 500, "0 abgelehnt, Default bleibt");

	zassert_equal(s.e.cfg.lock_pulse_ms, 1500);
	zassert_equal(s.e.cfg.contactor_monitor, 0);
	zassert_false(s.e.cfg.reject_state_d);
	zassert_equal(s.e.cfg.din.code[EVSE_DIN_LD], 22);
	zassert_equal(s.e.cfg.lock_cycle_gap_ms, 2000, "Default bleibt");
	zassert_equal(stored(525), 1500);
	zassert_equal(stored(412), 0);
	zassert_equal(stored(527), INT32_MIN);
	zassert_equal(fo.bytes + fo.erases, 0, "Sektor 3 nur gelesen");
}

ZTEST(evse_persist, test_import_refused)
{
	orig_image_init(fo.mem);
	orig_image_add(fo.mem, 525, 1500);
	sim_start(&s);
	zassert_equal(evse_import_orig(&s.e, &fo.fl, &n, s.now, report, NULL),
		      EVSE_IMP_E_UNSAFE);
	sim_boot(&s);
	fo.mem[0] = 0xEE; /* Originalbereich ungueltig */
	zassert_equal(evse_import_orig(&s.e, &fo.fl, &n, s.now, report, NULL), EVSE_IMP_E_ORIG);
	zassert_equal(s.e.cfg.lock_pulse_ms, 500);
}
