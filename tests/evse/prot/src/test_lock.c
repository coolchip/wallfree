/* SPDX-License-Identifier: Apache-2.0 */
/* Tests SOLL-LOCK-01..17 */
#include <zephyr/ztest.h>
#include <wallfree/evse/lock.h>

static struct evse_lock lk;
static struct evse_lock_config cfg;
static evse_ms_t now;
static bool ld_perm, ld_raw;
static int max_both_low; /* nie gleichzeitig aktiv */

/* 1-ms-Schritte; alle 8 ms ein neuer Messblock */
static void run(int ms)
{
	for (int i = 0; i < ms; i++) {
		now++;
		evse_lock_update(&lk, &cfg, ld_perm, ld_raw, (now % 8) == 0, now);
		if (lk.out_lock && lk.out_unlock) {
			max_both_low++;
		}
	}
}

/* bis Zyklusende laufen lassen, Dauer zurueckgeben */
static int run_cycle(void)
{
	int t = 0;

	while (evse_lock_busy(&lk) && t < 20000) {
		run(1);
		t++;
	}
	return t;
}

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_lock_init(&lk, EVSE_LOCK_UNLOCKED);
	cfg = (struct evse_lock_config){
		.d4 = true, .d6 = false, .ld_mode = EVSE_LD_PERMANENT,
		.lock_pulse_ms = 500, .unlock_pulse_ms = 500, .cycle_gap_ms = 2000,
	};
	now = 1000;
	ld_perm = ld_raw = false;
	max_both_low = 0;
}

ZTEST(evse_lock, test_no_d4_immediate)
{
	cfg.d4 = false;
	zassert_true(evse_lock_request(&lk, &cfg, true, now));
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);
	zassert_false(evse_lock_busy(&lk));
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_OK);
	zassert_false(lk.out_lock);
}

ZTEST(evse_lock, test_pulse_without_d6)
{
	zassert_true(evse_lock_request(&lk, &cfg, true, now));
	zassert_equal(lk.state, EVSE_LOCK_LOCKING);
	zassert_false(evse_lock_request(&lk, &cfg, false, now), "busy");
	run(1);
	zassert_true(lk.out_lock, "erster Zyklus ohne Wartezeit");
	run(498);
	zassert_true(lk.out_lock);
	run(2);
	zassert_false(lk.out_lock);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_OK);
	zassert_equal(max_both_low, 0);
}

ZTEST(evse_lock, test_gap_between_cycles)
{
	/* SOLL-LOCK-10 */
	evse_lock_request(&lk, &cfg, true, now);
	run_cycle();
	evse_ms_t end = now;

	evse_lock_request(&lk, &cfg, false, now);
	while (!lk.out_unlock) {
		run(1);
	}
	zassert_true(now - end >= 2000, "Abstand %lld", (long long)(now - end));
	zassert_true(now - end <= 2001);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);
	zassert_equal(max_both_low, 0);
}

ZTEST(evse_lock, test_permanent_ld)
{
	/* SOLL-LOCK-13 */
	cfg.d6 = true;
	evse_lock_request(&lk, &cfg, true, now);
	run(300);
	ld_perm = true; /* Kontakt schliesst waehrend der Fahrt */
	int t = run_cycle();

	zassert_true(t >= 500 + 500 + 10 - 300, "Settle 500 ms nach Impuls");
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_OK);

	/* Entriegeln, LD bleibt aktiv -> Misserfolg, bleibt LOCKED */
	evse_lock_request(&lk, &cfg, false, now);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_FAIL);
}

ZTEST(evse_lock, test_permanent_ld_missing)
{
	cfg.d6 = true;
	evse_lock_request(&lk, &cfg, true, now);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED, "Misserfolg: Gegenteil des Ziels");
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_FAIL);
}

static void pulse_ld_during(int start_ms, int len_ms)
{
	/* LD waehrend des Impulses kurz aktiv (Kuester: 12 V waehrend der Fahrt) */
	run(start_ms);
	ld_raw = true;
	run(len_ms);
	ld_raw = false;
}

ZTEST(evse_lock, test_pulsed_ld_ok)
{
	/* SOLL-LOCK-14 */
	cfg.d6 = true;
	cfg.ld_mode = EVSE_LD_PULSED;
	evse_lock_request(&lk, &cfg, true, now);
	pulse_ld_during(50, 200);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);
}

ZTEST(evse_lock, test_pulsed_ld_too_short)
{
	cfg.d6 = true;
	cfg.ld_mode = EVSE_LD_PULSED;
	evse_lock_request(&lk, &cfg, true, now);
	/* nur ein Block aktiv (Start auf Blockgrenze legen) */
	while ((now % 8) != 7) {
		run(1);
	}
	pulse_ld_during(0, 8);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);
}

ZTEST(evse_lock, test_pulsed_ld_stuck_high)
{
	/* Motor bleibt stehen: LD faellt nicht zurueck */
	cfg.d6 = true;
	cfg.ld_mode = EVSE_LD_PULSED;
	evse_lock_request(&lk, &cfg, true, now);
	run(50);
	ld_raw = true;
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);
}

ZTEST(evse_lock, test_pulsed_unlock_check)
{
	/* F27: ohne Option kein LD noetig, mit Option Fahrt erforderlich */
	cfg.d6 = true;
	cfg.ld_mode = EVSE_LD_PULSED;
	evse_lock_init(&lk, EVSE_LOCK_LOCKED);
	evse_lock_request(&lk, &cfg, false, now);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);

	evse_lock_init(&lk, EVSE_LOCK_LOCKED);
	cfg.unlock_check = true;
	evse_lock_request(&lk, &cfg, false, now);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_LOCKED);

	evse_lock_init(&lk, EVSE_LOCK_LOCKED);
	evse_lock_request(&lk, &cfg, false, now);
	pulse_ld_during(40, 100);
	run_cycle();
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);
}

ZTEST(evse_lock, test_d6_without_ld_fails)
{
	cfg.d6 = true;
	cfg.ld_mode = EVSE_LD_NONE;
	evse_lock_request(&lk, &cfg, true, now);
	ld_perm = true;
	run_cycle();
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_FAIL);
}

ZTEST(evse_lock, test_abort)
{
	/* SOLL-LOCK-17 */
	evse_lock_request(&lk, &cfg, true, now);
	run(100);
	zassert_true(lk.out_lock);
	evse_lock_abort(&lk, now);
	zassert_false(lk.out_lock);
	zassert_false(evse_lock_busy(&lk));
	zassert_equal(lk.state, EVSE_LOCK_UNLOCKED);
	zassert_equal(evse_lock_take_result(&lk), EVSE_LOCK_RES_FAIL);
}

ZTEST(evse_lock, test_max_pulse)
{
	cfg.lock_pulse_ms = 3000;
	evse_lock_request(&lk, &cfg, true, now);
	run(1);
	evse_ms_t start = now;

	while (lk.out_lock) {
		run(1);
	}
	zassert_equal(now - start, 3000);
}

ZTEST_SUITE(evse_lock, NULL, NULL, before, NULL, NULL);
