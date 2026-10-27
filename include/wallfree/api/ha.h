/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Home-Assistant-Erkennung ueber MQTT (SOLL-MQTT-40..42): Konfigurationsnachrichten unter
 * homeassistant/<komponente>/<knoten>/<objekt>/config, retained. Home Assistant legt daraus
 * die Wallbox als Geraet mit Sensoren an; die Werte kommen aus den vorhandenen Topics
 * (SOLL-MQTT-10), es gibt keine zusaetzlichen Zustandsnachrichten. Reines C.
 */
#ifndef WALLFREE_API_HA_H_
#define WALLFREE_API_HA_H_

#include <stdbool.h>
#include <stddef.h>
#include <wallfree/api/json.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_HA_PREFIX      "homeassistant"
#define WF_HA_PAYLOAD_MAX 640 /**< groesste Konfiguration (Test mit 64 Zeichen Praefix) */
#define WF_HA_TOPIC_MAX   96

struct wf_ha_ctx {
	const char *base;    /**< Topic-Praefix der Wallbox, z. B. "wallfree/a1b2c3" */
	const char *node;    /**< Kennung des Geraets, z. B. "wallfree_a1b2c3" (a-z, 0-9, _) */
	const char *name;    /**< Geraetename in Home Assistant */
	const char *version; /**< Firmwarestand */
	bool commands;       /**< mqtt_commands: Ladestrom schreibbar */
	int power_idx;       /**< Index der Wirkleistung in meters/0/values, -1 = keiner */
	int energy_idx;      /**< Index der Energie (Bezug) in meters/0/values, -1 = keiner */
};

/**
 * Konfiguration Nummer @p i erzeugen.
 * @return 1: Topic und Nutzdaten erzeugt; 0: Element entfaellt (z. B. ohne Zaehler), Topic
 *         erzeugt, leere retained Nachricht loescht es in Home Assistant; -1: keine weiteren
 */
int wf_ha_config(int i, const struct wf_ha_ctx *c, char *topic, size_t topic_size,
		 struct wf_jbuf *b);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_HA_H_ */
