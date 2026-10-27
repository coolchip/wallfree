/* SPDX-License-Identifier: Apache-2.0 */
/* Ereignisprotokoll, siehe include/wallfree/api/alog.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/alog.h>

void wf_alog_init(struct wf_alog *l)
{
	memset(l, 0, sizeof(*l));
	l->magic = WF_ALOG_MAGIC;
	l->version = WF_ALOG_VERSION;
	l->boot = 1;
	l->next_seq = 1;
	l->first_seq = 1;
}

static uint32_t oldest(const struct wf_alog *l)
{
	uint32_t o = l->next_seq > WF_ALOG_SIZE ? l->next_seq - WF_ALOG_SIZE : 1;

	return l->first_seq > o ? l->first_seq : o;
}

/* Nach Stromausfall steht Zufall im Backup-SRAM: nur uebernehmen, was zusammenpasst */
static bool plausible(const struct wf_alog *l)
{
	if (l->magic != WF_ALOG_MAGIC || l->version != WF_ALOG_VERSION || l->next_seq == 0 ||
	    l->boot == 0 || l->first_seq == 0 || l->first_seq > l->next_seq) {
		return false;
	}
	for (uint32_t s = oldest(l); s < l->next_seq; s++) {
		const struct wf_alog_entry *e = &l->e[s % WF_ALOG_SIZE];

		if (e->seq != s || e->boot == 0 || e->boot > l->boot ||
		    e->kind > WF_ALOG_CLEARED) {
			return false;
		}
	}
	return true;
}

bool wf_alog_restore(struct wf_alog *l)
{
	if (!plausible(l) || l->boot == UINT16_MAX) {
		wf_alog_init(l);
		return false;
	}
	l->boot++;
	l->wall_base = 0;
	return true;
}

void wf_alog_set_wall_base(struct wf_alog *l, int64_t wall_base)
{
	l->wall_base = wall_base;
}

static struct wf_alog_entry *newest(struct wf_alog *l)
{
	return l->next_seq > l->first_seq ? &l->e[(l->next_seq - 1) % WF_ALOG_SIZE] : NULL;
}

static void push(struct wf_alog *l, struct wf_alog_entry en)
{
	en.boot = l->boot;
	en.wall = l->wall_base != 0 ? l->wall_base + en.t : 0;
	en.seq = l->next_seq++;
	l->e[en.seq % WF_ALOG_SIZE] = en;
}

void wf_alog_auth_fail(struct wf_alog *l, uint32_t addr, int64_t t)
{
	struct wf_alog_entry *n = newest(l);

	if (n != NULL && n->kind == WF_ALOG_AUTH_FAIL && n->addr == addr && n->boot == l->boot) {
		n->t = t;
		n->wall = l->wall_base != 0 ? l->wall_base + t : 0;
		if (n->count < UINT16_MAX) {
			n->count++;
		}
		return;
	}
	push(l, (struct wf_alog_entry){.t = t, .kind = WF_ALOG_AUTH_FAIL, .count = 1, .addr = addr});
}

void wf_alog_event(struct wf_alog *l, const struct wf_event *ev)
{
	push(l, (struct wf_alog_entry){.t = ev->t, .kind = WF_ALOG_EVENT, .ev = *ev});
}

void wf_alog_clear(struct wf_alog *l, uint32_t addr, int64_t t)
{
	l->first_seq = l->next_seq;
	push(l, (struct wf_alog_entry){.t = t, .kind = WF_ALOG_CLEARED, .addr = addr});
}

bool wf_alog_get(const struct wf_alog *l, uint32_t *seq, struct wf_alog_entry *out)
{
	uint32_t s = *seq < oldest(l) ? oldest(l) : *seq;

	if (s >= l->next_seq) {
		return false;
	}
	*out = l->e[s % WF_ALOG_SIZE];
	*seq = s + 1;
	return true;
}

void wf_alog_entry_json(const struct wf_alog_entry *e, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "seq", true);
	wf_jb_int(b, e->seq);
	wf_jb_key(b, "t_ms", false);
	wf_jb_int(b, e->t);
	wf_jb_key(b, "boot", false);
	wf_jb_int(b, e->boot);
	if (e->wall != 0) {
		wf_jb_key(b, "wall_ms", false);
		wf_jb_int(b, e->wall);
	}
	wf_jb_key(b, "kind", false);
	if (e->kind == WF_ALOG_AUTH_FAIL || e->kind == WF_ALOG_CLEARED) {
		char a[16];

		snprintf(a, sizeof(a), "%u.%u.%u.%u", (unsigned int)(e->addr >> 24),
			 (unsigned int)(e->addr >> 16) & 255u, (unsigned int)(e->addr >> 8) & 255u,
			 (unsigned int)e->addr & 255u);
		wf_jb_str(b, e->kind == WF_ALOG_CLEARED ? "cleared" : "auth_fail");
		wf_jb_key(b, "addr", false);
		wf_jb_str(b, a);
		if (e->kind == WF_ALOG_AUTH_FAIL) {
			wf_jb_key(b, "count", false);
			wf_jb_int(b, e->count);
		}
	} else {
		wf_jb_str(b, "event");
		wf_jb_key(b, "event", false);
		wf_ev_json(&e->ev, b);
	}
	wf_jb_raw(b, "}");
}

/* Text nur anhaengen, wenn er ganz passt; sonst bleibt b unveraendert */
static bool append(struct wf_jbuf *b, const struct wf_alog_entry *e, bool first)
{
	size_t len = b->len;

	if (!first) {
		wf_jb_raw(b, ",");
	}
	if (e != NULL) {
		wf_alog_entry_json(e, b);
	} else {
		wf_jb_raw(b, "]}");
	}
	if (b->overflow) {
		b->overflow = false;
		b->len = len;
		b->p[len] = '\0';
		return false;
	}
	return true;
}

uint32_t wf_alog_json(const struct wf_alog *l, uint32_t cursor, struct wf_jbuf *b)
{
	bool first = cursor == 0;
	bool wrote = false;
	struct wf_alog_entry e;
	uint32_t seq = cursor;

	if (first) {
		wf_jb_raw(b, "{\"boot\":");
		wf_jb_int(b, l->boot);
		wf_jb_raw(b, ",\"entries\":[");
	}
	for (;;) {
		uint32_t at = seq;

		if (!wf_alog_get(l, &seq, &e)) {
			if (!append(b, NULL, true)) {
				return at != 0 ? at : l->next_seq; /* Ende im naechsten Teil */
			}
			return WF_ALOG_DONE;
		}
		if (append(b, &e, first)) {
			first = false;
			wrote = true;
		} else if (wrote) {
			return e.seq; /* im naechsten Teil */
		}
		/* passt allein nicht: uebersprungen */
	}
}
