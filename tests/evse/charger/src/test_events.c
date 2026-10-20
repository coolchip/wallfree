/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Ereignisse an die Schnittstellen (SOLL-ERR-10) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/events.h>
#include "sim.h"

#define DIP(n) ((evse_dip_t)(1u << ((n) - 1)))

static struct evse_config cfg;
static struct sim s;
static struct wf_event log_ev[256];
static size_t n_log;

static void before(void *f)
{
	ARG_UNUSED(f);
	evse_config_defaults(&cfg);
	cfg.dip = 0;
	cfg.contactor_monitor = EVSE_CTM_SENSE;
	sim_init(&s, &cfg);
	n_log = 0;
}

ZTEST_SUITE(api_events, NULL, NULL, before, NULL, NULL);

/* wie der Automatenthread: Vergleich vor/nach jedem Schritt */
static void run(int ms)
{
	for (int i = 0; i < ms; i++) {
		struct wf_ev_snap a, b;
		struct wf_event ev[WF_EV_MAX_DIFF];

		wf_ev_snap_take(&s.e, &a);
		sim_run(&s, 1);
		wf_ev_snap_take(&s.e, &b);
		size_t n = wf_ev_diff(&a, &b, s.now, ev);

		for (size_t k = 0; k < n && n_log < ARRAY_SIZE(log_ev); k++) {
			log_ev[n_log++] = ev[k];
		}
	}
}

static int count(uint8_t type, int a)
{
	int c = 0;

	for (size_t i = 0; i < n_log; i++) {
		if (log_ev[i].type == type && (a < 0 || log_ev[i].a == a)) {
			c++;
		}
	}
	return c;
}

static int find_state(uint8_t to, size_t from)
{
	for (size_t i = from; i < n_log; i++) {
		if (log_ev[i].type == WF_EV_STATE && log_ev[i].b == to) {
			return (int)i;
		}
	}
	return -1;
}

ZTEST(api_events, test_diff)
{
	struct wf_ev_snap a = {.state = 1, .lock = EVSE_LOCK_UNLOCKED, .errors = 0x3};
	struct wf_ev_snap b = a;
	struct wf_event ev[WF_EV_MAX_DIFF];

	zassert_equal(wf_ev_diff(&a, &b, 5, ev), 0);
	b.errors = 0x6;
	b.contactor = true;
	b.lock = EVSE_LOCK_LOCKING; /* Zwischenschritt: kein Ereignis */
	b.state = 2;
	zassert_equal(wf_ev_diff(&a, &b, 5, ev), 3);
	zassert_equal(ev[0].type, WF_EV_ERROR);
	zassert_equal(ev[0].set, 0x4);
	zassert_equal(ev[0].cleared, 0x1);
	zassert_equal(ev[1].type, WF_EV_CONTACTOR);
	zassert_equal(ev[1].a, 1);
	zassert_equal(ev[2].type, WF_EV_STATE);
	zassert_equal(ev[2].a, 1);
	zassert_equal(ev[2].b, 2);
	zassert_equal(ev[2].t, 5);
	a = b;
	b.lock = EVSE_LOCK_LOCKED;
	zassert_equal(wf_ev_diff(&a, &b, 6, ev), 1);
	zassert_equal(ev[0].type, WF_EV_LOCK);
	zassert_equal(ev[0].a, EVSE_LOCK_LOCKED);
}

ZTEST(api_events, test_ring)
{
	struct wf_ev_ring r;
	struct wf_event ev;
	uint32_t seq = 1;

	wf_ev_ring_init(&r);
	zassert_false(wf_ev_ring_get(&r, &seq, &ev));
	for (int i = 0; i < 3; i++) {
		wf_ev_ring_push(&r, (struct wf_event){.t = i});
	}
	for (int i = 0; i < 3; i++) {
		zassert_true(wf_ev_ring_get(&r, &seq, &ev));
		zassert_equal(ev.seq, (uint32_t)i + 1);
		zassert_equal(ev.t, i);
	}
	zassert_false(wf_ev_ring_get(&r, &seq, &ev));
	zassert_equal(seq, 4);

	/* Ueberlauf: Leser bekommt das aelteste vorhandene, Luecke an seq sichtbar */
	for (int i = 3; i < 3 + WF_EV_RING + 5; i++) {
		wf_ev_ring_push(&r, (struct wf_event){.t = i});
	}
	zassert_true(wf_ev_ring_get(&r, &seq, &ev));
	zassert_equal(ev.seq, 3 + 5 + 1, "aeltestes vorhandenes");
	int n = 1;

	while (wf_ev_ring_get(&r, &seq, &ev)) {
		n++;
	}
	zassert_equal(n, WF_EV_RING);
	zassert_equal(ev.seq, 3 + WF_EV_RING + 5);
}

ZTEST(api_events, test_json)
{
	char buf[200];
	struct wf_jbuf b;
	const struct {
		struct wf_event ev;
		const char *json;
	} t[] = {
		{{.seq = 1, .t = 12, .type = WF_EV_STATE, .a = 1, .b = 5},
		 "{\"seq\":1,\"t_ms\":12,\"type\":\"state\",\"from\":1,\"to\":5}"},
		{{.seq = 2, .t = 13, .type = WF_EV_CONTACTOR, .a = 1},
		 "{\"seq\":2,\"t_ms\":13,\"type\":\"contactor\",\"on\":true}"},
		{{.seq = 3, .t = 14, .type = WF_EV_LOCK, .a = EVSE_LOCK_LOCKED},
		 "{\"seq\":3,\"t_ms\":14,\"type\":\"lock\",\"lock_state\":3}"},
		{{.seq = 4, .t = 15, .type = WF_EV_ERROR, .set = EVSE_ERR_RCM_DC6, .cleared = 0},
		 "{\"seq\":4,\"t_ms\":15,\"type\":\"error\",\"set\":[\"RCM-DC6\"],\"cleared\":[]}"},
	};

	for (size_t i = 0; i < ARRAY_SIZE(t); i++) {
		wf_jb_init(&b, buf, sizeof(buf));
		wf_ev_json(&t[i].ev, &b);
		zassert_false(b.overflow);
		zassert_str_equal(buf, t[i].json);
	}
}

/* Steckvorgang mit Verriegelung: Zustaende, Verriegelung, Schuetz in richtiger Reihenfolge */
ZTEST(api_events, test_charging_cycle)
{
	cfg.dip = DIP(4);
	sim_init(&s, &cfg);
	sim_boot(&s);
	n_log = 0;
	s.car = EVSE_CP_B;
	run(8000);
	s.car = EVSE_CP_C;
	run(3000);
	zassert_equal(s.e.state, EVSE_S6_CHARGING, "Zustand %d", s.e.state);
	s.car = EVSE_CP_A;
	run(8000);
	zassert_equal(s.e.state, EVSE_S1_IDLE, "Zustand %d", s.e.state);

	int i_lock = find_state(EVSE_S2_LOCKING, 0);
	int i_chg = find_state(EVSE_S6_CHARGING, 0);
	int i_unl = find_state(EVSE_S8_UNLOCKING, 0);

	zassert_true(i_lock >= 0 && i_chg > i_lock && i_unl > i_chg);
	zassert_true(find_state(EVSE_S1_IDLE, i_unl) > i_unl);
	zassert_equal(count(WF_EV_CONTACTOR, 1), 1);
	zassert_equal(count(WF_EV_CONTACTOR, 0), 1);
	zassert_equal(count(WF_EV_LOCK, EVSE_LOCK_LOCKED), 1);
	zassert_equal(count(WF_EV_LOCK, EVSE_LOCK_UNLOCKED), 1);
	zassert_equal(count(WF_EV_ERROR, -1), 0);
	/* Schuetz ein erst im Laden, aus vor dem Entriegeln */
	for (size_t i = 0; i < n_log; i++) {
		if (log_ev[i].type == WF_EV_CONTACTOR) {
			zassert_true(log_ev[i].a ? (int)i >= i_chg - 1 : (int)i < i_unl);
		}
	}
}

/* Fehlerstrom beim Laden: Fehlerereignis, Schuetz aus, Zustand S9 */
ZTEST(api_events, test_rcm_fault)
{
	sim_boot(&s);
	s.car = EVSE_CP_C;
	run(8000);
	zassert_equal(s.e.state, EVSE_S6_CHARGING);
	n_log = 0;
	s.rcm_fault_current = true;
	run(200);
	zassert_true(n_log >= 3, "%u Ereignisse", (unsigned int)n_log);
	zassert_equal(log_ev[0].type, WF_EV_ERROR);
	zassert_true(log_ev[0].set & EVSE_ERR_RCM_ANY);
	zassert_equal(count(WF_EV_CONTACTOR, 0), 1);
	zassert_true(find_state(EVSE_S9_ERROR, 0) >= 0);
}
