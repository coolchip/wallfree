/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Home-Assistant-Erkennung (SOLL-MQTT-40..42) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/ha.h>

ZTEST_SUITE(api_ha, NULL, NULL, NULL, NULL, NULL);

static char topic[WF_HA_TOPIC_MAX];
static char buf[WF_HA_PAYLOAD_MAX];

static int gen(int i, const struct wf_ha_ctx *c)
{
	struct wf_jbuf b;

	wf_jb_init(&b, buf, sizeof(buf));
	return wf_ha_config(i, c, topic, sizeof(topic), &b);
}

/* Wertvorlage vollstaendig: "{{ ... }}" (Testgeraet 2026-10-27: Zustand war auf 96 Byte gekuerzt) */
static void check_tpl(void)
{
	const char *t = strstr(buf, "\"val_tpl\":\"{{ ");
	const char *end;

	zassert_not_null(t, "%s", buf);
	end = strchr(t + 11, '"');
	zassert_not_null(end);
	zassert_true(end - t > 14 && strncmp(end - 3, " }}", 3) == 0, "%.*s", (int)(end - t), t);
}

ZTEST(api_ha, test_configs)
{
	const struct wf_ha_ctx c = {.base = "wallfree/a1b2c3", .node = "wallfree_a1b2c3",
				    .name = "Wallfree a1b2c3", .version = "0.1+2e0fb17",
				    .commands = false, .power_idx = -1, .energy_idx = -1};
	int made = 0, removed = 0, i;

	for (i = 0; gen(i, &c) >= 0; i++) {
		zassert_true(strncmp(topic, "homeassistant/", 14) == 0, "%s", topic);
		zassert_not_null(strstr(topic, "/wallfree_a1b2c3/"), "%s", topic);
		zassert_true(strcmp(topic + strlen(topic) - 7, "/config") == 0);
		if (gen(i, &c) == 1) {
			made++;
			zassert_not_null(strstr(buf, "\"~\":\"wallfree/a1b2c3\""));
			zassert_not_null(strstr(buf, "\"avty_t\":\"~/online\""));
			zassert_not_null(strstr(buf, "\"ids\":[\"wallfree_a1b2c3\"]"));
			zassert_not_null(strstr(buf, "\"uniq_id\":\"wallfree_a1b2c3_"));
			check_tpl();
		} else {
			removed++;
		}
	}
	zassert_equal(made, 6, "Zustand, angeschlossen, laedt, Fehler, erlaubt, Vorgabe");
	zassert_equal(removed, 3, "ohne Befehle und Zaehler: Ladestrom, Leistung, Energie entfallen");

	zassert_equal(gen(0, &c), 1);
	zassert_str_equal(topic, "homeassistant/sensor/wallfree_a1b2c3/state/config");
	zassert_not_null(strstr(buf, "\"stat_t\":\"~/evse/state\""));
	zassert_not_null(strstr(buf, "Lädt"), "Texte in UTF-8");
}

ZTEST(api_ha, test_commands_and_meter)
{
	const struct wf_ha_ctx c = {.base = "wb", .node = "wallfree_a1b2c3", .name = "W",
				    .version = "x", .commands = true, .power_idx = 7,
				    .energy_idx = 9};
	int i, n = 0;

	for (i = 0; gen(i, &c) >= 0; i++) {
		n += gen(i, &c) == 1;
		check_tpl();
		if (strstr(topic, "/number/") != NULL) {
			zassert_not_null(strstr(buf, "\"cmd_t\":\"~/evse/external_current_update\""));
			zassert_not_null(strstr(buf, "\"cmd_tpl\":\"{{ (value * 1000) | int }}\""));
		}
		if (strstr(topic, "/power/") != NULL) {
			zassert_not_null(strstr(buf, "value_json[7]"));
			zassert_not_null(strstr(buf, "\"unit_of_meas\":\"W\""));
		}
		if (strstr(topic, "/energy/") != NULL) {
			zassert_not_null(strstr(buf, "value_json[9]"));
			zassert_not_null(strstr(buf, "\"stat_cla\":\"total_increasing\""));
		}
	}
	zassert_equal(n, 9, "alle Elemente");
	zassert_equal(gen(-1, &c), -1);
}

/* groesste Nutzdaten mit dem laengsten zulaessigen Praefix (64 Zeichen) passen */
ZTEST(api_ha, test_size)
{
	static const char base[] =
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
	const struct wf_ha_ctx c = {.base = base, .node = "wallfree_a1b2c3",
				    .name = "wallfree-1234567890123456789012345", .version = "0.2-dev+2e0fb17-dirty",
				    .commands = true, .power_idx = 7, .energy_idx = 9};

	zassert_equal(strlen(base), 64);
	for (int i = 0; gen(i, &c) >= 0; i++) {
		zassert_equal(gen(i, &c), 1, "Element %d passt nicht", i);
	}
}
