/* SPDX-License-Identifier: Apache-2.0 */
/* MQTT ohne Netzwerk, siehe include/wallfree/api/mqtt.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/mqtt.h>

uint32_t wf_mq_hash(const char *s, size_t n)
{
	uint32_t h = 2166136261u;

	for (size_t i = 0; i < n; i++) {
		h = (h ^ (uint8_t)s[i]) * 16777619u;
	}
	return h;
}

void wf_mq_reset(struct wf_mq_topic *t)
{
	t->valid = false;
	t->pending = false;
}

void wf_mq_update(struct wf_mq_topic *t, uint32_t hash)
{
	if (!t->valid || hash != t->sent_hash) {
		t->pending = true;
	}
}

void wf_mq_touch(struct wf_mq_topic *t)
{
	t->pending = true;
}

bool wf_mq_due(const struct wf_mq_topic *t, int64_t now, uint32_t interval_ms)
{
	if (!t->valid) {
		return true; /* Vollversand nach dem Verbinden, ohne Bremse */
	}
	return t->pending && now - t->sent_ms >= (int64_t)interval_ms;
}

void wf_mq_sent(struct wf_mq_topic *t, uint32_t hash, int64_t now)
{
	t->sent_hash = hash;
	t->sent_ms = now;
	t->valid = true;
	t->pending = false;
}

int wf_mq_topic_name(char *out, size_t size, const char *prefix, const char *name)
{
	int n = snprintf(out, size, "%s/%s", prefix, name);

	return n < 0 || (size_t)n >= size ? -1 : n;
}

enum wf_mq_in wf_mq_dispatch(const struct wf_mq_cmd *cmds, size_t n, const char *prefix,
			     const char *topic, size_t topic_len, const char *payload, size_t len,
			     bool retained, void *ctx, const char **err)
{
	size_t pl = strlen(prefix);
	const struct wf_mq_cmd *c = NULL;

	if (topic_len > pl && memcmp(topic, prefix, pl) == 0 && topic[pl] == '/') {
		const char *rest = topic + pl + 1;
		size_t rl = topic_len - pl - 1;

		for (size_t i = 0; i < n; i++) {
			if (strlen(cmds[i].name) == rl && memcmp(cmds[i].name, rest, rl) == 0) {
				c = &cmds[i];
				break;
			}
		}
	}
	if (c == NULL) {
		*err = "unbekanntes Topic";
		return WF_MQ_IN_UNKNOWN;
	}
	if (retained) {
		*err = "gespeicherte Nachricht (retained) ignoriert";
		return WF_MQ_IN_RETAINED;
	}
	if (len == 0) {
		*err = "leere Nachricht ignoriert";
		return WF_MQ_IN_EMPTY;
	}
	if (len > WF_MQ_PAYLOAD_MAX || payload == NULL) {
		*err = "Nachricht zu lang (hoechstens 128 Byte)";
		return WF_MQ_IN_TOO_LONG;
	}
	*err = c->handle(ctx, payload, len);
	return *err == NULL ? WF_MQ_IN_OK : WF_MQ_IN_REJECTED;
}

void wf_mq_note_command(struct wf_mq_status *s, enum wf_mq_in r, const char *err)
{
	s->commands++;
	if (r != WF_MQ_IN_OK) {
		s->rejected++;
		snprintf(s->last_reject, sizeof(s->last_reject), "%s", err);
	}
}

void wf_mq_status_json(const struct wf_mq_status *s, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "connection_state", true);
	wf_jb_int(b, s->conn);
	wf_jb_key(b, "connection_start", false);
	wf_jb_int(b, s->start_ms);
	wf_jb_key(b, "connection_end", false);
	wf_jb_int(b, s->end_ms);
	wf_jb_key(b, "last_error", false);
	wf_jb_int(b, s->last_error);
	wf_jb_key(b, "published", false);
	wf_jb_int(b, s->published);
	wf_jb_key(b, "publish_errors", false);
	wf_jb_int(b, s->publish_errors);
	wf_jb_key(b, "commands", false);
	wf_jb_int(b, s->commands);
	wf_jb_key(b, "rejected", false);
	wf_jb_int(b, s->rejected);
	wf_jb_key(b, "last_reject", false);
	wf_jb_str(b, s->last_reject);
	wf_jb_key(b, "prefix", false);
	wf_jb_str(b, s->prefix);
	wf_jb_raw(b, "}");
}
