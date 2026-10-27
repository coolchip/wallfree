/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Log (SOLL-ERR-11..13) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/alog.h>

static struct wf_alog l;

static void before(void *f)
{
	ARG_UNUSED(f);
	wf_alog_init(&l);
}

ZTEST_SUITE(api_alog, NULL, NULL, before, NULL, NULL);

#define A 0xC0A80114 /* 192.168.1.20 */
#define B 0x0A000002

static struct wf_event state_ev(int64_t t, uint8_t from, uint8_t to)
{
	return (struct wf_event){.seq = 7, .t = t, .type = WF_EV_STATE, .a = from, .b = to};
}

ZTEST(api_alog, test_entries_in_order)
{
	struct wf_alog_entry e;
	uint32_t seq = 1;
	struct wf_event ev = state_ev(200, 1, 2);

	zassert_false(wf_alog_get(&l, &seq, &e));
	wf_alog_auth_fail(&l, A, 100);
	wf_alog_event(&l, &ev);
	wf_alog_auth_fail(&l, B, 300);

	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, 1);
	zassert_equal(e.kind, WF_ALOG_AUTH_FAIL);
	zassert_equal(e.addr, A);
	zassert_equal(e.count, 1);
	zassert_equal(e.t, 100);
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.kind, WF_ALOG_EVENT);
	zassert_equal(e.t, 200, "Zeit aus dem Ereignis");
	zassert_equal(e.ev.seq, 7, "Ereignisnummer bleibt erhalten");
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, 3);
	zassert_equal(e.addr, B);
	zassert_false(wf_alog_get(&l, &seq, &e));
}

/* Durchprobieren von einer Adresse belegt nur einen Eintrag */
ZTEST(api_alog, test_repeated_failures_merge)
{
	struct wf_alog_entry e;
	uint32_t seq = 1;
	struct wf_event ev = state_ev(50, 1, 2);

	for (int i = 0; i < 100000; i++) {
		wf_alog_auth_fail(&l, A, 1000 + i);
	}
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.count, UINT16_MAX, "Zaehler bleibt stehen");
	zassert_equal(e.t, 1000 + 99999, "Zeit des letzten Versuchs");
	zassert_false(wf_alog_get(&l, &seq, &e));

	/* andere Adresse oder Ereignis dazwischen: neuer Eintrag */
	wf_alog_auth_fail(&l, B, 2);
	wf_alog_event(&l, &ev);
	wf_alog_auth_fail(&l, B, 3);
	zassert_equal(l.next_seq, 5);
}

ZTEST(api_alog, test_overflow_drops_oldest)
{
	struct wf_alog_entry e;
	uint32_t seq = 1;

	for (int i = 0; i < WF_ALOG_SIZE + 5; i++) {
		struct wf_event ev = state_ev(i, 1, 2);

		wf_alog_event(&l, &ev);
	}
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, 6, "aeltestes vorhandenes, Luecke an seq");
	int n = 1;

	while (wf_alog_get(&l, &seq, &e)) {
		n++;
	}
	zassert_equal(n, WF_ALOG_SIZE);
	zassert_equal(e.seq, WF_ALOG_SIZE + 5);
}

ZTEST(api_alog, test_entry_json)
{
	char buf[300];
	struct wf_jbuf b;
	struct wf_alog_entry fail = {.seq = 4, .t = 5300, .kind = WF_ALOG_AUTH_FAIL, .count = 3,
				     .addr = A, .boot = 2};
	struct wf_alog_entry ev = {.seq = 5, .t = 6100, .kind = WF_ALOG_EVENT, .boot = 3,
				   .wall = 1793000000123,
				   .ev = {.seq = 9, .t = 6100, .type = WF_EV_CONTACTOR, .a = 1}};

	wf_jb_init(&b, buf, sizeof(buf));
	wf_alog_entry_json(&fail, &b);
	zassert_str_equal(buf, "{\"seq\":4,\"t_ms\":5300,\"boot\":2,\"kind\":\"auth_fail\","
			       "\"addr\":\"192.168.1.20\",\"count\":3}");
	wf_jb_init(&b, buf, sizeof(buf));
	wf_alog_entry_json(&ev, &b);
	zassert_str_equal(buf, "{\"seq\":5,\"t_ms\":6100,\"boot\":3,\"wall_ms\":1793000000123,"
			       "\"kind\":\"event\",\"event\":"
			       "{\"seq\":9,\"t_ms\":6100,\"type\":\"contactor\",\"on\":true}}");
}

/* Ausgabe in Teilen ergibt zusammengesetzt dasselbe wie in einem Stueck */
static size_t render(size_t chunk, char *all, size_t all_size)
{
	char buf[1024];
	struct wf_jbuf b;
	uint32_t cur = 0;
	size_t len = 0;
	int parts = 0;

	zassert_true(chunk <= sizeof(buf));
	do {
		wf_jb_init(&b, buf, chunk);
		cur = wf_alog_json(&l, cur, &b);
		zassert_false(b.overflow);
		zassert_true(b.len > 0, "jeder Teil bringt Fortschritt");
		zassert_true(len + b.len < all_size);
		memcpy(all + len, buf, b.len);
		len += b.len;
		zassert_true(++parts < 1000);
	} while (cur != WF_ALOG_DONE);
	all[len] = '\0';
	return len;
}

ZTEST(api_alog, test_json_empty)
{
	static char all[64];

	render(400, all, sizeof(all));
	zassert_str_equal(all, "{\"boot\":1,\"entries\":[]}");
}

ZTEST(api_alog, test_json_chunked)
{
	static char one[16384], parts[16384];

	for (int i = 0; i < WF_ALOG_SIZE + 3; i++) {
		struct wf_event ev = {.t = i, .type = WF_EV_ERROR, .set = EVSE_ERR_RCM_DC6 |
						     EVSE_ERR_CONTACTOR, .cleared = EVSE_ERR_LOCK};

		if (i % 3 == 0) {
			wf_alog_auth_fail(&l, A + (uint32_t)i, i);
		} else {
			wf_alog_event(&l, &ev);
		}
	}
	size_t n1 = render(1024, one, sizeof(one));
	size_t n2 = render(400, parts, sizeof(parts));

	zassert_equal(n1, n2);
	zassert_str_equal(one, parts);
	zassert_true(strncmp(parts, "{\"boot\":1,\"entries\":[{\"seq\":4,", 30) == 0, "aeltester zuerst");
	zassert_true(strcmp(parts + n2 - 3, "}]}") == 0);
	zassert_is_null(strstr(parts, ",,"));
	zassert_is_null(strstr(parts, "[,"));
}

/* SOLL-ERR-12: Inhalt uebersteht einen Neustart, Zufall nach Stromausfall nicht */
ZTEST(api_alog, test_restore)
{
	struct wf_alog_entry e;
	struct wf_event ev = {.type = WF_EV_STATE, .a = 0, .b = 1};
	uint32_t seq = 0;

	wf_alog_event(&l, &ev);
	wf_alog_auth_fail(&l, A, 200);
	zassert_equal(l.boot, 1);
	zassert_true(wf_alog_restore(&l), "stimmiger Inhalt bleibt");
	zassert_equal(l.boot, 2);
	wf_alog_auth_fail(&l, A, 50); /* neuer Start: nicht mit dem alten Eintrag zusammenfassen */
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.boot, 1);
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.count, 1);
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.boot, 2);
	zassert_equal(e.seq, 3, "Nummern laufen ueber den Neustart weiter");

	/* Uhrzeit: nur Eintraege nach dem Setzen tragen sie */
	zassert_equal(e.wall, 0);
	wf_alog_set_wall_base(&l, 1793000000000);
	wf_alog_event(&l, &(struct wf_event){.t = 700, .type = WF_EV_CONTACTOR});
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.wall, 1793000000700);
	zassert_true(wf_alog_restore(&l));
	zassert_equal(l.wall_base, 0, "nach Neustart wieder unbekannt");

	/* Stromausfall: Zufall bzw. beschaedigte Nummern -> neu */
	memset(&l, 0x5a, sizeof(l));
	zassert_false(wf_alog_restore(&l));
	zassert_equal(l.boot, 1);
	zassert_equal(l.next_seq, 1);
	wf_alog_event(&l, &ev);
	l.e[1].seq = 7;
	zassert_false(wf_alog_restore(&l));
	wf_alog_event(&l, &ev);
	l.version++;
	zassert_false(wf_alog_restore(&l));
}

/* SOLL-ERR-13: Loeschen hinterlaesst einen Eintrag mit Absender, Nummern laufen weiter */
ZTEST(api_alog, test_clear)
{
	struct wf_alog_entry e;
	struct wf_event ev = state_ev(100, 1, 2);
	uint32_t seq = 0;
	char buf[256];
	struct wf_jbuf b;

	wf_alog_auth_fail(&l, A, 50);
	wf_alog_event(&l, &ev);
	wf_alog_clear(&l, B, 900);
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, 3, "Nummern laufen weiter");
	zassert_equal(e.kind, WF_ALOG_CLEARED);
	zassert_equal(e.addr, B);
	zassert_false(wf_alog_get(&l, &seq, &e), "aeltere Eintraege verworfen");

	wf_alog_auth_fail(&l, B, 950); /* nicht mit dem Loescheintrag zusammenfassen */
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.kind, WF_ALOG_AUTH_FAIL);
	zassert_equal(e.count, 1);

	wf_jb_init(&b, buf, sizeof(buf));
	zassert_equal(wf_alog_json(&l, 0, &b), WF_ALOG_DONE);
	zassert_str_equal(buf, "{\"boot\":1,\"entries\":["
			       "{\"seq\":3,\"t_ms\":900,\"boot\":1,\"kind\":\"cleared\",\"addr\":\"10.0.0.2\"},"
			       "{\"seq\":4,\"t_ms\":950,\"boot\":1,\"kind\":\"auth_fail\",\"addr\":\"10.0.0.2\","
			       "\"count\":1}]}");

	zassert_true(wf_alog_restore(&l), "geloeschter Stand uebersteht den Neustart");
	seq = 0;
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, 3);

	/* nach dem Loeschen voller Umlauf: wie ohne Loeschen */
	for (int i = 0; i < WF_ALOG_SIZE + 5; i++) {
		wf_alog_event(&l, &ev);
	}
	seq = 0;
	zassert_true(wf_alog_get(&l, &seq, &e));
	zassert_equal(e.seq, l.next_seq - WF_ALOG_SIZE);
}
