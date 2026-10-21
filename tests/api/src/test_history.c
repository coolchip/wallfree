/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Verlauf (SOLL-API-33) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/history.h>

#define IV WF_HIST_INTERVAL_MS

static struct wf_hist h;

static void before(void *f)
{
	ARG_UNUSED(f);
	wf_hist_init(&h);
}

ZTEST_SUITE(api_history, NULL, NULL, before, NULL, NULL);

ZTEST(api_history, test_mean_per_interval)
{
	struct wf_hist_row r;

	zassert_false(wf_hist_current(&h, &r));
	wf_hist_add(&h, 0, 1000, 16000, WF_HIST_CONNECTED);
	wf_hist_add(&h, 1000, 3000, 6000, WF_HIST_CONNECTED | WF_HIST_CHARGING);
	zassert_equal(wf_hist_len(&h), 0, "Intervall laeuft noch");
	zassert_true(wf_hist_current(&h, &r));
	zassert_equal(r.power_w, 2000);
	zassert_equal(r.allowed, 44, "11 A in 0,25 A");
	zassert_equal(r.flags, WF_HIST_CONNECTED | WF_HIST_CHARGING, "ODER");

	wf_hist_add(&h, IV, 0, 0, 0);
	zassert_equal(wf_hist_len(&h), 1);
	zassert_true(wf_hist_get(&h, 0, &r));
	zassert_equal(r.power_w, 2000);
	zassert_false(wf_hist_get(&h, 1, &r));
	zassert_equal(h.end_ms, IV);
	zassert_true(wf_hist_current(&h, &r));
	zassert_equal(r.flags, 0, "neues Intervall beginnt leer");
}

ZTEST(api_history, test_invalid_power_ignored)
{
	struct wf_hist_row r;

	wf_hist_add(&h, 0, -1, 0, 0);
	wf_hist_add(&h, 1, 500, 0, 0);
	wf_hist_add(&h, IV, -1, 0, 0);
	wf_hist_add(&h, 2 * IV, -1, 0, 0);
	zassert_true(wf_hist_get(&h, 0, &r));
	zassert_equal(r.power_w, 500, "nur gueltige Werte im Mittel");
	zassert_true(wf_hist_get(&h, 1, &r));
	zassert_equal(r.power_w, WF_HIST_NO_POWER);
}

ZTEST(api_history, test_gap_and_wrap)
{
	struct wf_hist_row r;

	wf_hist_add(&h, 0, 100, 0, 0);
	wf_hist_add(&h, 3 * IV, 0, 0, 0);
	zassert_equal(wf_hist_len(&h), 3, "zwei leere Intervalle nachgetragen");
	zassert_true(wf_hist_get(&h, 1, &r));
	zassert_equal(r.power_w, WF_HIST_NO_POWER);

	for (int i = 0; i < WF_HIST_SIZE + 5; i++) {
		wf_hist_add(&h, (4 + i) * (int64_t)IV, i, 0, 0);
	}
	zassert_equal(wf_hist_len(&h), WF_HIST_SIZE);
	zassert_true(wf_hist_get(&h, WF_HIST_SIZE - 1, &r));
	zassert_equal(r.power_w, WF_HIST_SIZE + 3, "juengste Zeile zuletzt");
	zassert_true(wf_hist_get(&h, 0, &r));
	zassert_equal(r.power_w, 4, "aelteste herausgefallen");

	wf_hist_add(&h, 0, 1, 0, 0);
	zassert_equal(wf_hist_len(&h), WF_HIST_SIZE, "rueckwaerts ignoriert");
}

ZTEST(api_history, test_json_small)
{
	char buf[256];
	struct wf_jbuf b;

	wf_jb_init(&b, buf, sizeof(buf));
	zassert_equal(wf_hist_json(&h, 5, 0, &b), WF_HIST_DONE);
	zassert_str_equal(buf, "{\"interval_ms\":600000,\"rows\":[],\"age_ms\":5,\"current\":null}");

	wf_hist_add(&h, 0, 1520, 16000, 3);
	wf_hist_add(&h, IV, -1, 0, 0);
	wf_jb_init(&b, buf, sizeof(buf));
	zassert_equal(wf_hist_json(&h, IV + 7, 0, &b), WF_HIST_DONE);
	zassert_str_equal(buf, "{\"interval_ms\":600000,\"rows\":[[1520,16000,3]],\"age_ms\":7,"
			       "\"current\":[null,0,0]}");
}

/* Teile zusammengesetzt = ein Stueck; ein Intervallende zwischen zwei Teilen haengt an */
ZTEST(api_history, test_json_chunks)
{
	static char all[16384];
	char part[128];
	struct wf_jbuf b;
	uint32_t cur = 0;
	int parts = 0;

	for (int i = 0; i <= WF_HIST_SIZE + 10; i++) {
		wf_hist_add(&h, (int64_t)i * IV, 22000, 32000, 7);
	}
	all[0] = '\0';
	do {
		wf_jb_init(&b, part, sizeof(part));
		cur = wf_hist_json(&h, 0, cur, &b);
		zassert_false(b.overflow);
		strcat(all, part);
		if (parts++ == 3) {
			wf_hist_add(&h, (int64_t)(WF_HIST_SIZE + 11) * IV, 0, 0, 0);
		}
	} while (cur != WF_HIST_DONE);
	zassert_true(parts > 20);
	zassert_not_null(strstr(all, "{\"interval_ms\":600000,\"rows\":[[22000,32000,7],[22000"));
	zassert_not_null(strstr(all, "[22000,32000,7],[22000,32000,7]],\"age_ms\":"), "%s", all);
	zassert_is_null(strstr(all, ",,"));
	zassert_is_null(strstr(all, "[,"));
	int rows = 0;

	for (const char *p = strstr(all, "\"rows\":[") + 8; *p && *p != '"'; p++) {
		rows += *p == '[';
	}
	zassert_equal(rows, WF_HIST_SIZE + 1, "eine Zeile waehrend der Ausgabe dazugekommen");
}
