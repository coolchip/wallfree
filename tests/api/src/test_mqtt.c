/* SPDX-License-Identifier: Apache-2.0 */
/* Tests MQTT ohne Netzwerk (12.6: SOLL-MQTT-10/11/20/22/30) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/mqtt.h>

ZTEST_SUITE(api_mqtt, NULL, NULL, NULL, NULL, NULL);

ZTEST(api_mqtt, test_topic_name)
{
	char t[27];

	zassert_equal(wf_mq_topic_name(t, sizeof(t), "wallfree/a1b2c3", "evse/state"), 26);
	zassert_str_equal(t, "wallfree/a1b2c3/evse/state");
	zassert_equal(wf_mq_topic_name(t, sizeof(t), "wallfree/a1b2c3", "evse/limits"), -1,
		      "zu lang");
	zassert_equal(wf_mq_topic_name(t, sizeof(t), "w", "online"), 8);
	zassert_str_equal(t, "w/online");
}

/* SOLL-MQTT-11: Vollversand nach Verbinden, Aenderung, Mindestabstand, Nachholen */
ZTEST(api_mqtt, test_send_rule)
{
	struct wf_mq_topic t = {0};
	uint32_t a = wf_mq_hash("{\"a\":1}", 7), b = wf_mq_hash("{\"a\":2}", 7);

	zassert_not_equal(a, b);
	wf_mq_reset(&t);
	wf_mq_update(&t, a);
	zassert_true(wf_mq_due(&t, 0, 1000), "nach Verbinden sofort");
	wf_mq_sent(&t, a, 0);
	wf_mq_update(&t, a);
	zassert_false(wf_mq_due(&t, 5000, 1000), "unveraendert");

	wf_mq_update(&t, b);
	zassert_true(wf_mq_due(&t, 5000, 1000), "Aenderung nach Ablauf");
	wf_mq_sent(&t, b, 5000);
	wf_mq_update(&t, a);
	zassert_false(wf_mq_due(&t, 5500, 1000), "Mindestabstand");
	wf_mq_update(&t, b); /* zurueck auf den gesendeten Stand: bleibt ausstehend */
	zassert_true(wf_mq_due(&t, 6000, 1000), "nachgeholt mit dem dann aktuellen Stand");
	wf_mq_sent(&t, b, 6000);
	zassert_false(wf_mq_due(&t, 9000, 1000));

	/* Intervall 0: jede Aenderung sofort */
	wf_mq_update(&t, a);
	zassert_true(wf_mq_due(&t, 6000, 0));

	/* Zaehlerzyklus ohne Vergleich */
	wf_mq_sent(&t, a, 6000);
	wf_mq_touch(&t);
	zassert_false(wf_mq_due(&t, 6999, 1000));
	zassert_true(wf_mq_due(&t, 7000, 1000));

	/* neue Verbindung: sofort, ohne Bremse */
	wf_mq_sent(&t, a, 7000);
	wf_mq_reset(&t);
	zassert_true(wf_mq_due(&t, 7001, 1000));
}

struct fake {
	int calls;
	char last[WF_MQ_PAYLOAD_MAX + 1];
};

static const char *h_current(void *ctx, const char *payload, size_t len)
{
	struct fake *f = ctx;

	f->calls++;
	memcpy(f->last, payload, len);
	f->last[len] = '\0';
	return strcmp(f->last, "5") == 0 ? "ungueltig" : NULL;
}

static const char *h_other(void *ctx, const char *payload, size_t len)
{
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	((struct fake *)ctx)->calls += 100;
	return NULL;
}

static const struct wf_mq_cmd cmds[] = {
	{"evse/external_current_update", h_current},
	{"evse/other", h_other},
};

static enum wf_mq_in in(struct fake *f, const char *topic, const char *payload, size_t len,
			bool retained, const char **err)
{
	return wf_mq_dispatch(cmds, ARRAY_SIZE(cmds), "wf/1", topic, strlen(topic), payload, len,
			      retained, f, err);
}

/* SOLL-MQTT-20/22 */
ZTEST(api_mqtt, test_dispatch)
{
	struct fake f = {0};
	const char *err = NULL;
	char big[WF_MQ_PAYLOAD_MAX + 1];

	zassert_equal(in(&f, "wf/1/evse/external_current_update", "16000", 5, false, &err),
		      WF_MQ_IN_OK);
	zassert_is_null(err);
	zassert_equal(f.calls, 1);
	zassert_str_equal(f.last, "16000");
	zassert_equal(in(&f, "wf/1/evse/other", "1", 1, false, &err), WF_MQ_IN_OK);
	zassert_equal(f.calls, 101, "zweiter Tabelleneintrag");

	zassert_equal(in(&f, "wf/1/evse/external_current_update", "5", 1, false, &err),
		      WF_MQ_IN_REJECTED);
	zassert_str_equal(err, "ungueltig");
	zassert_equal(in(&f, "wf/1/evse/external_current_update", "16000", 5, true, &err),
		      WF_MQ_IN_RETAINED);
	zassert_equal(in(&f, "wf/1/evse/external_current_update", "", 0, false, &err),
		      WF_MQ_IN_EMPTY);
	memset(big, '1', sizeof(big));
	zassert_equal(in(&f, "wf/1/evse/external_current_update", big, WF_MQ_PAYLOAD_MAX, false,
			 &err), WF_MQ_IN_OK, "128 Byte gehen");
	zassert_equal(in(&f, "wf/1/evse/external_current_update", NULL, WF_MQ_PAYLOAD_MAX + 1,
			 false, &err), WF_MQ_IN_TOO_LONG);
	zassert_equal(f.calls, 103, "Ignoriertes erreicht die Behandlung nicht");

	static const char *const unknown[] = {"wf/1/evse/external_current", "wf/2/evse/other",
					      "wf/1evse/other", "wf/1/evse/other/x", "wf/1/",
					      "wf/1", "evse/other"};

	for (size_t i = 0; i < ARRAY_SIZE(unknown); i++) {
		zassert_equal(in(&f, unknown[i], "1", 1, false, &err), WF_MQ_IN_UNKNOWN, "%s",
			      unknown[i]);
	}
	zassert_equal(f.calls, 103);
}

/* SOLL-MQTT-30 */
ZTEST(api_mqtt, test_status)
{
	struct wf_mq_status s = {.conn = WF_MQ_CONNECTED, .start_ms = 1500, .last_error = -111,
				 .published = 7};
	char buf[320];
	struct wf_jbuf b;

	strcpy(s.prefix, "wallfree/a1b2c3");
	wf_mq_note_command(&s, WF_MQ_IN_OK, NULL);
	wf_mq_note_command(&s, WF_MQ_IN_RETAINED, "gespeicherte Nachricht (retained) ignoriert");
	wf_jb_init(&b, buf, sizeof(buf));
	wf_mq_status_json(&s, &b);
	zassert_false(b.overflow);
	zassert_str_equal(buf, "{\"connection_state\":2,\"connection_start\":1500,"
			       "\"connection_end\":0,\"last_error\":-111,\"published\":7,"
			       "\"publish_errors\":0,\"commands\":2,\"rejected\":1,"
			       "\"last_reject\":\"gespeicherte Nachricht (retained) ignoriert\","
			       "\"prefix\":\"wallfree/a1b2c3\"}");
}
