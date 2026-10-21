/* SPDX-License-Identifier: Apache-2.0 */
/*
 * MQTT ohne Netzwerk (Soll-Spezifikation 12.6, F30): Versandregel je Topic,
 * Befehlstabelle, Status als JSON. Reines C, auf dem PC testbar; Verbindung
 * und Protokoll liegen in app/src/mqtt_task.c.
 */
#ifndef WALLFREE_API_MQTT_H_
#define WALLFREE_API_MQTT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wallfree/api/json.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_MQ_PAYLOAD_MAX 128 /**< groesste angenommene Befehlsnachricht (SOLL-MQTT-22) */

/** FNV-1a ueber die Nutzdaten (Aenderungserkennung ohne Kopie). */
uint32_t wf_mq_hash(const char *s, size_t n);

/** Versandzustand eines Zustandstopics (SOLL-MQTT-11). */
struct wf_mq_topic {
	uint32_t sent_hash;
	int64_t sent_ms;
	bool valid;   /**< seit dem Verbinden gesendet */
	bool pending; /**< Aenderung oder Zyklus seit dem letzten Versand */
};

/** Nach dem Verbinden: naechster Aufruf von wf_mq_due() ist sofort faellig. */
void wf_mq_reset(struct wf_mq_topic *t);

/** Aktuellen Stand melden: Aenderung gegenueber dem Gesendeten merkt sich das Topic. */
void wf_mq_update(struct wf_mq_topic *t, uint32_t hash);

/** Senden noetig ohne Vergleich (Zaehlerzyklus). */
void wf_mq_touch(struct wf_mq_topic *t);

/** Faellig: nie gesendet, oder ausstehend und Mindestabstand @p interval_ms vorbei. */
bool wf_mq_due(const struct wf_mq_topic *t, int64_t now, uint32_t interval_ms);

/** Erfolgreich gesendet. */
void wf_mq_sent(struct wf_mq_topic *t, uint32_t hash, int64_t now);

/** Topic "<prefix>/<name>" bilden. @return Laenge oder -1, wenn @p size nicht reicht */
int wf_mq_topic_name(char *out, size_t size, const char *prefix, const char *name);

/**
 * Befehl (SOLL-MQTT-20): Name ohne Praefix und Behandlung. Die Behandlung
 * prueft und setzt ueber die gemeinsame Zugriffsschicht.
 * @return NULL oder Fehlertext
 */
struct wf_mq_cmd {
	const char *name;
	const char *(*handle)(void *ctx, const char *payload, size_t len);
};

enum wf_mq_in {
	WF_MQ_IN_OK = 0,
	WF_MQ_IN_UNKNOWN,  /**< kein Befehlstopic */
	WF_MQ_IN_RETAINED, /**< gespeicherte Nachricht (SOLL-MQTT-22) */
	WF_MQ_IN_EMPTY,
	WF_MQ_IN_TOO_LONG,
	WF_MQ_IN_REJECTED, /**< Behandlung lehnt ab */
};

/**
 * Eingegangene Nachricht zuordnen und ausfuehren. @p payload darf NULL sein,
 * wenn @p len > WF_MQ_PAYLOAD_MAX (nicht gelesen).
 * @param err Klartext bei Ergebnis != WF_MQ_IN_OK
 */
enum wf_mq_in wf_mq_dispatch(const struct wf_mq_cmd *cmds, size_t n, const char *prefix,
			     const char *topic, size_t topic_len, const char *payload, size_t len,
			     bool retained, void *ctx, const char **err);

/** Verbindungszustand wie WARP mqtt/state (SOLL-MQTT-30) */
enum wf_mq_conn {
	WF_MQ_OFF = 0,
	WF_MQ_NOT_CONNECTED,
	WF_MQ_CONNECTED,
	WF_MQ_ERROR, /**< Broker lehnt die Verbindung ab */
};

struct wf_mq_status {
	uint8_t conn;         /**< enum wf_mq_conn */
	int64_t start_ms;     /**< Beginn der letzten Verbindung, 0 = nie */
	int64_t end_ms;       /**< Ende der letzten Verbindung, 0 = nie */
	int32_t last_error;   /**< 0, negativer Netzfehler oder CONNACK-Code */
	uint32_t published;
	uint32_t publish_errors;
	uint32_t commands;    /**< Nachrichten auf Befehlstopics */
	uint32_t rejected;    /**< davon ignoriert oder abgelehnt */
	char last_reject[64];
	char prefix[65];      /**< wirksames Praefix */
};

/** Ergebnis eines Befehls in den Status uebernehmen (Zaehler, letzter Text). */
void wf_mq_note_command(struct wf_mq_status *s, enum wf_mq_in r, const char *err);

void wf_mq_status_json(const struct wf_mq_status *s, struct wf_jbuf *b);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_MQTT_H_ */
