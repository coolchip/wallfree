/* SPDX-License-Identifier: Apache-2.0 */
/* JSON der Zaehlerwerte, siehe include/wallfree/api/meter_json.h */
#include <wallfree/api/meter_json.h>

void wf_jb_milli(struct wf_jbuf *b, int64_t v)
{
	char s[24];
	char *p = s + sizeof(s);
	uint64_t u = v < 0 ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
	int digits = 0;

	*--p = '\0';
	do {
		*--p = (char)('0' + u % 10);
		u /= 10;
		if (++digits == 3) {
			*--p = '.';
		}
	} while (u > 0 || digits < 4);
	if (v < 0) {
		*--p = '-';
	}
	wf_jb_raw(b, p);
}

void wf_meter_value_ids_json(uint8_t type, struct wf_jbuf *b)
{
	bool first = true;

	wf_jb_raw(b, "[");
	for (int q = 0; q < METER_Q_COUNT; q++) {
		if (meter_has(type, (enum meter_q)q)) {
			wf_jb_raw(b, first ? "" : ",");
			wf_jb_int(b, meter_warp_id((enum meter_q)q));
			first = false;
		}
	}
	wf_jb_raw(b, "]");
}

void wf_meter_values_json(const struct meter_values *mv, struct wf_jbuf *b)
{
	bool first = true;

	wf_jb_raw(b, "[");
	for (int q = 0; q < METER_Q_COUNT; q++) {
		if (!meter_has(mv->type, (enum meter_q)q)) {
			continue;
		}
		wf_jb_raw(b, first ? "" : ",");
		if (mv->valid[q]) {
			wf_jb_milli(b, mv->v[q]);
		} else {
			wf_jb_raw(b, "null");
		}
		first = false;
	}
	wf_jb_raw(b, "]");
}

void wf_meter_state_json(const struct meter_values *mv, struct wf_jbuf *b)
{
	static const struct {
		const char *name;
		size_t off;
	} cnt[] = {
		{"requests", offsetof(struct meter_stats, requests)},
		{"responses", offsetof(struct meter_stats, responses)},
		{"timeouts", offsetof(struct meter_stats, timeouts)},
		{"frame_errors", offsetof(struct meter_stats, frame_errors)},
		{"exceptions", offsetof(struct meter_stats, exceptions)},
		{"implausible", offsetof(struct meter_stats, implausible)},
		{"sets", offsetof(struct meter_stats, sets)},
	};
	/* WARP: 0 nicht konfiguriert, 1 nicht verbunden, 2 verbunden */
	int state = mv->status == METER_OFF ? 0 : mv->status == METER_OK ? 2 : 1;

	wf_jb_raw(b, "{");
	wf_jb_key(b, "state", true);
	wf_jb_int(b, state);
	wf_jb_key(b, "type", false);
	wf_jb_int(b, mv->type);
	wf_jb_key(b, "status", false);
	wf_jb_str(b, meter_status_name(mv->status));
	wf_jb_key(b, "age_ms", false);
	wf_jb_int(b, mv->age_ms);
	for (size_t i = 0; i < sizeof(cnt) / sizeof(cnt[0]); i++) {
		wf_jb_key(b, cnt[i].name, false);
		wf_jb_int(b, *(const uint32_t *)((const char *)&mv->st + cnt[i].off));
	}
	wf_jb_key(b, "last_exception", false);
	wf_jb_int(b, mv->st.last_exception);
	wf_jb_raw(b, "}");
}
