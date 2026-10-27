/* SPDX-License-Identifier: Apache-2.0 */
/* Home-Assistant-Erkennung, siehe include/wallfree/api/ha.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/ha.h>

enum ha_need {
	NEED_NONE = 0,
	NEED_COMMANDS,
	NEED_POWER,
	NEED_ENERGY,
};

/* Abkuerzungen nach der Home-Assistant-MQTT-Dokumentation (stat_t, val_tpl, ...) */
struct ha_entity {
	const char *comp;  /**< sensor, binary_sensor, number */
	const char *obj;   /**< Objektkennung, Teil von Topic und unique_id */
	const char *name;
	const char *topic; /**< relativ zum Praefix der Wallbox */
	const char *tpl;   /**< Wertvorlage; %d = Index bei Zaehlerwerten */
	const char *unit;
	const char *dev_cla;
	const char *stat_cla;
	enum ha_need need;
};

static const struct ha_entity ents[] = {
	{"sensor", "state", "Zustand", "evse/state",
	 "{{ ['Kein Fahrzeug','Wartet auf Freigabe','Bereit','Lädt','Fehler']"
	 "[value_json.charger_state] }}", NULL, NULL, NULL, NEED_NONE},
	{"binary_sensor", "plugged", "Fahrzeug angeschlossen", "evse/state",
	 "{{ 'ON' if value_json.iec61851_state in [1, 2] else 'OFF' }}", NULL, "plug", NULL,
	 NEED_NONE},
	{"binary_sensor", "charging", "Lädt", "evse/state",
	 "{{ 'ON' if value_json.charger_state == 3 else 'OFF' }}", NULL, "battery_charging", NULL,
	 NEED_NONE},
	{"binary_sensor", "error", "Fehler", "evse/state",
	 "{{ 'ON' if value_json.error_state != 0 else 'OFF' }}", NULL, "problem", NULL, NEED_NONE},
	{"sensor", "allowed_current", "Erlaubter Strom", "evse/state",
	 "{{ value_json.allowed_charging_current / 1000 }}", "A", "current", "measurement",
	 NEED_NONE},
	{"sensor", "set_current", "Vorgabe Ladestrom", "evse/external_current",
	 "{{ value_json.current / 1000 }}", "A", "current", NULL, NEED_NONE},
	{"number", "charge_current", "Ladestrom", "evse/external_current",
	 "{{ value_json.current / 1000 }}", "A", "current", NULL, NEED_COMMANDS},
	{"sensor", "power", "Leistung", "meters/0/values", "{{ value_json[%d] }}", "W", "power",
	 "measurement", NEED_POWER},
	{"sensor", "energy", "Energie", "meters/0/values", "{{ value_json[%d] }}", "kWh", "energy",
	 "total_increasing", NEED_ENERGY},
};

static void key_str(struct wf_jbuf *b, const char *k, const char *v, bool first)
{
	wf_jb_key(b, k, first);
	wf_jb_str(b, v);
}

int wf_ha_config(int i, const struct wf_ha_ctx *c, char *topic, size_t topic_size,
		 struct wf_jbuf *b)
{
	char s[96];

	if (i < 0 || (size_t)i >= sizeof(ents) / sizeof(ents[0])) {
		return -1;
	}
	const struct ha_entity *e = &ents[i];
	int n = snprintf(topic, topic_size, WF_HA_PREFIX "/%s/%s/%s/config", e->comp, c->node,
			 e->obj);

	if (n < 0 || (size_t)n >= topic_size) {
		topic[0] = '\0';
		return -1;
	}
	int idx = e->need == NEED_POWER ? c->power_idx : e->need == NEED_ENERGY ? c->energy_idx : 0;

	if ((e->need == NEED_COMMANDS && !c->commands) || idx < 0) {
		return 0;
	}
	wf_jb_raw(b, "{");
	key_str(b, "~", c->base, true);
	key_str(b, "name", e->name, false);
	snprintf(s, sizeof(s), "%s_%s", c->node, e->obj);
	key_str(b, "uniq_id", s, false);
	snprintf(s, sizeof(s), "~/%s", e->topic);
	key_str(b, "stat_t", s, false);
	snprintf(s, sizeof(s), e->tpl, idx);
	key_str(b, "val_tpl", s, false);
	if (e->unit != NULL) {
		key_str(b, "unit_of_meas", e->unit, false);
	}
	if (e->dev_cla != NULL) {
		key_str(b, "dev_cla", e->dev_cla, false);
	}
	if (e->stat_cla != NULL) {
		key_str(b, "stat_cla", e->stat_cla, false);
	}
	if (e->need == NEED_COMMANDS) {
		/* SOLL-MQTT-20: Befehl in mA; 0 = Laden aus, 1..5 A lehnt die Steuerung ab */
		key_str(b, "cmd_t", "~/evse/external_current_update", false);
		key_str(b, "cmd_tpl", "{{ (value * 1000) | int }}", false);
		wf_jb_raw(b, ",\"min\":0,\"max\":80,\"step\":1,\"mode\":\"box\"");
	}
	/* SOLL-MQTT-04: Erreichbarkeit ueber den Last Will */
	wf_jb_raw(b, ",\"avty_t\":\"~/online\",\"pl_avail\":\"true\",\"pl_not_avail\":\"false\"");
	wf_jb_raw(b, ",\"dev\":{");
	wf_jb_key(b, "ids", true);
	wf_jb_raw(b, "[");
	wf_jb_str(b, c->node);
	wf_jb_raw(b, "]");
	key_str(b, "name", c->name, false);
	key_str(b, "mf", "Wallfree", false);
	key_str(b, "mdl", "EV-CC-AC1-M3-CBC-RCM-ETH", false);
	key_str(b, "sw", c->version, false);
	wf_jb_raw(b, "}}");
	return b->overflow ? -1 : 1;
}
