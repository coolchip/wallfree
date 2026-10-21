/* SPDX-License-Identifier: Apache-2.0 */
/* Verlauf (SOLL-API-33) */
#include <string.h>
#include <wallfree/api/history.h>

#define ROW_MAX  20 /* ",[65534,63750,255]" */
#define TAIL_MAX 64 /* "],\"age_ms\":<13 Ziffern>,\"current\":[...]}" */

void wf_hist_init(struct wf_hist *h)
{
	memset(h, 0, sizeof(*h));
	h->acc.slot = -1;
}

static void commit(struct wf_hist *h, const struct wf_hist_row *row)
{
	h->r[h->count % WF_HIST_SIZE] = *row;
	h->count++;
}

static void acc_row(const struct wf_hist_acc *a, struct wf_hist_row *out)
{
	uint64_t q = a->n ? (a->a_sum / a->n + 125) / 250 : 0;
	uint64_t p = a->p_n ? a->p_sum / a->p_n : 0;

	out->power_w = a->p_n == 0 ? WF_HIST_NO_POWER : p >= WF_HIST_NO_POWER ? WF_HIST_NO_POWER - 1
									    : (uint16_t)p;
	out->allowed = q > UINT8_MAX ? UINT8_MAX : (uint8_t)q;
	out->flags = a->flags;
}

void wf_hist_add(struct wf_hist *h, int64_t t_ms, int32_t power_w, int32_t allowed_ma,
		 uint8_t flags)
{
	int64_t slot = t_ms / WF_HIST_INTERVAL_MS;
	struct wf_hist_acc *a = &h->acc;

	if (a->slot < 0) {
		a->slot = slot;
	}
	if (slot < a->slot) {
		return; /* Zeit laeuft nicht rueckwaerts */
	}
	if (slot > a->slot) {
		const struct wf_hist_row empty = {.power_w = WF_HIST_NO_POWER};
		struct wf_hist_row row;
		int64_t gap = slot - a->slot - 1;

		acc_row(a, &row);
		commit(h, &row);
		for (int64_t i = 0; i < gap && i < WF_HIST_SIZE; i++) {
			commit(h, &empty);
		}
		h->end_ms = slot * WF_HIST_INTERVAL_MS;
		memset(a, 0, sizeof(*a));
		a->slot = slot;
	}
	a->n++;
	a->a_sum += allowed_ma > 0 ? (uint64_t)allowed_ma : 0;
	if (power_w >= 0) {
		a->p_n++;
		a->p_sum += (uint64_t)power_w;
	}
	a->flags |= flags;
}

uint32_t wf_hist_len(const struct wf_hist *h)
{
	return h->count < WF_HIST_SIZE ? h->count : WF_HIST_SIZE;
}

bool wf_hist_get(const struct wf_hist *h, uint32_t i, struct wf_hist_row *out)
{
	if (i >= wf_hist_len(h)) {
		return false;
	}
	*out = h->r[(h->count - wf_hist_len(h) + i) % WF_HIST_SIZE];
	return true;
}

bool wf_hist_current(const struct wf_hist *h, struct wf_hist_row *out)
{
	if (h->acc.n == 0) {
		return false;
	}
	acc_row(&h->acc, out);
	return true;
}

static void row_json(struct wf_jbuf *b, const struct wf_hist_row *r)
{
	wf_jb_raw(b, "[");
	if (r->power_w == WF_HIST_NO_POWER) {
		wf_jb_raw(b, "null");
	} else {
		wf_jb_int(b, r->power_w);
	}
	wf_jb_raw(b, ",");
	wf_jb_int(b, (int64_t)r->allowed * 250);
	wf_jb_raw(b, ",");
	wf_jb_int(b, r->flags);
	wf_jb_raw(b, "]");
}

/* Cursor = 1 + laufende Nummer der naechsten Zeile (unabhaengig vom Umlauf) */
uint32_t wf_hist_json(const struct wf_hist *h, int64_t now_ms, uint32_t cursor, struct wf_jbuf *b)
{
	uint32_t oldest = h->count - wf_hist_len(h);
	uint32_t i = cursor == 0 || cursor - 1 < oldest ? oldest : cursor - 1;
	bool comma = cursor != 0;
	struct wf_hist_row row;

	if (cursor == 0) {
		wf_jb_raw(b, "{\"interval_ms\":");
		wf_jb_int(b, WF_HIST_INTERVAL_MS);
		wf_jb_raw(b, ",\"rows\":[");
	}
	for (; i < h->count; i++) {
		if (b->size - b->len < ROW_MAX + TAIL_MAX) {
			return i + 1;
		}
		if (comma) {
			wf_jb_raw(b, ",");
		}
		comma = true;
		row_json(b, &h->r[i % WF_HIST_SIZE]);
	}
	wf_jb_raw(b, "],\"age_ms\":");
	wf_jb_int(b, now_ms - h->end_ms);
	wf_jb_raw(b, ",\"current\":");
	if (wf_hist_current(h, &row)) {
		row_json(b, &row);
	} else {
		wf_jb_raw(b, "null");
	}
	wf_jb_raw(b, "}");
	return WF_HIST_DONE;
}
