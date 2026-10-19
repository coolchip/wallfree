/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Bremse gegen Passwort-Durchprobieren (SOLL-API-07) */
#include <zephyr/ztest.h>
#include <wallfree/api/ratelimit.h>

static struct wf_rl rl;

static void before(void *f)
{
	ARG_UNUSED(f);
	wf_rl_init(&rl);
}

ZTEST_SUITE(api_ratelimit, NULL, NULL, before, NULL, NULL);

#define A 0x0A000001
#define B 0x0A000002

ZTEST(api_ratelimit, test_free_attempts_then_lock)
{
	for (int i = 0; i < WF_RL_FREE - 1; i++) {
		wf_rl_fail(&rl, A, 1000);
		zassert_equal(wf_rl_blocked(&rl, A, 1000), 0, "Versuch %d frei", i + 1);
	}
	wf_rl_fail(&rl, A, 1000);
	zassert_equal(wf_rl_blocked(&rl, A, 1000), WF_RL_LOCK_MS);
	zassert_equal(wf_rl_blocked(&rl, B, 1000), 0, "andere Adresse nicht betroffen");
	zassert_equal(wf_rl_blocked(&rl, A, 1000 + WF_RL_LOCK_MS), 0, "Sperre abgelaufen");

	/* jeder weitere Fehlversuch verdoppelt bis zum Hoechstwert */
	int64_t t = 1000 + WF_RL_LOCK_MS;

	wf_rl_fail(&rl, A, t);
	zassert_equal(wf_rl_blocked(&rl, A, t), 2 * WF_RL_LOCK_MS);
	for (int i = 0; i < 20; i++) {
		wf_rl_fail(&rl, A, t);
	}
	zassert_equal(wf_rl_blocked(&rl, A, t), WF_RL_LOCK_MAX_MS);
}

ZTEST(api_ratelimit, test_success_resets)
{
	for (int i = 0; i < WF_RL_FREE - 1; i++) {
		wf_rl_fail(&rl, A, 0);
	}
	wf_rl_ok(&rl, A);
	wf_rl_fail(&rl, A, 0);
	zassert_equal(wf_rl_blocked(&rl, A, 0), 0);
}

ZTEST(api_ratelimit, test_forget_after_quiet_hour)
{
	for (int i = 0; i < WF_RL_FREE - 1; i++) {
		wf_rl_fail(&rl, A, 0);
	}
	wf_rl_fail(&rl, A, WF_RL_FORGET_MS + 1);
	zassert_equal(wf_rl_blocked(&rl, A, WF_RL_FORGET_MS + 1), 0, "Zaehler verfallen");
}

ZTEST(api_ratelimit, test_table_full_keeps_locked_entries)
{
	/* A sperren */
	for (int i = 0; i < WF_RL_FREE; i++) {
		wf_rl_fail(&rl, A, 100);
	}
	/* viele andere Adressen mit je einem Fehlversuch, spaeter */
	for (uint32_t k = 0; k < 3 * WF_RL_SLOTS; k++) {
		wf_rl_fail(&rl, 0x0B000000 + k, 200 + k);
	}
	zassert_true(wf_rl_blocked(&rl, A, 300) > 0, "gesperrter Eintrag wird nicht verdraengt");
}
