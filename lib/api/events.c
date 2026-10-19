/* SPDX-License-Identifier: Apache-2.0 */
/* Ereignisse an die Schnittstellen, siehe include/wallfree/api/events.h */
#include <string.h>
#include <wallfree/api/events.h>
#include <wallfree/api/evse_api.h>

void wf_ev_snap_take(const struct evse *e, struct wf_ev_snap *s)
{
	s->state = (uint8_t)e->state;
	s->lock = (uint8_t)e->lock.state;
	s->contactor = e->contactor;
	s->errors = e->errors;
}

static bool lock_final(uint8_t st)
{
	return st == EVSE_LOCK_UNLOCKED || st == EVSE_LOCK_LOCKED || st == EVSE_LOCK_ERROR;
}

size_t wf_ev_diff(const struct wf_ev_snap *old, const struct wf_ev_snap *cur, evse_ms_t t,
		  struct wf_event out[WF_EV_MAX_DIFF])
{
	size_t n = 0;

	if (old->errors != cur->errors) {
		out[n++] = (struct wf_event){.t = t, .type = WF_EV_ERROR,
					     .set = cur->errors & ~old->errors,
					     .cleared = old->errors & ~cur->errors};
	}
	if (old->contactor != cur->contactor) {
		out[n++] = (struct wf_event){.t = t, .type = WF_EV_CONTACTOR, .a = cur->contactor};
	}
	/* Verriegelungsergebnis: nur Endzustaende, Zwischenschritte nicht */
	if (old->lock != cur->lock && lock_final(cur->lock)) {
		out[n++] = (struct wf_event){.t = t, .type = WF_EV_LOCK, .a = cur->lock};
	}
	if (old->state != cur->state) {
		out[n++] = (struct wf_event){.t = t, .type = WF_EV_STATE, .a = old->state,
					     .b = cur->state};
	}
	return n;
}

void wf_ev_ring_init(struct wf_ev_ring *r)
{
	memset(r, 0, sizeof(*r));
	r->next_seq = 1;
}

void wf_ev_ring_push(struct wf_ev_ring *r, struct wf_event ev)
{
	ev.seq = r->next_seq++;
	r->ev[ev.seq % WF_EV_RING] = ev;
}

bool wf_ev_ring_get(const struct wf_ev_ring *r, uint32_t *seq, struct wf_event *out)
{
	uint32_t oldest = r->next_seq > WF_EV_RING ? r->next_seq - WF_EV_RING : 1;
	uint32_t s = *seq < oldest ? oldest : *seq;

	if (s >= r->next_seq) {
		return false;
	}
	*out = r->ev[s % WF_EV_RING];
	*seq = s + 1;
	return true;
}

static const char *const type_names[] = {"state", "error", "lock", "contactor"};

void wf_ev_json(const struct wf_event *ev, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "seq", true);
	wf_jb_int(b, ev->seq);
	wf_jb_key(b, "t_ms", false);
	wf_jb_int(b, ev->t);
	wf_jb_key(b, "type", false);
	wf_jb_str(b, ev->type < 4 ? type_names[ev->type] : "?");
	switch (ev->type) {
	case WF_EV_STATE:
		wf_jb_key(b, "from", false);
		wf_jb_int(b, ev->a);
		wf_jb_key(b, "to", false);
		wf_jb_int(b, ev->b);
		break;
	case WF_EV_ERROR:
		wf_jb_key(b, "set", false);
		api_json_err_list(b, ev->set);
		wf_jb_key(b, "cleared", false);
		api_json_err_list(b, ev->cleared);
		break;
	case WF_EV_LOCK:
		wf_jb_key(b, "lock_state", false);
		wf_jb_int(b, ev->a);
		break;
	case WF_EV_CONTACTOR:
		wf_jb_key(b, "on", false);
		wf_jb_raw(b, ev->a ? "true" : "false");
		break;
	default:
		break;
	}
	wf_jb_raw(b, "}");
}
