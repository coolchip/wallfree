/* SPDX-License-Identifier: Apache-2.0 */
/*
 * MQTT-Client (Soll-Spezifikation 12.6, F30): Zustaende als WARP-JSON an den
 * Broker, Befehle aus einer Tabelle. Eigener Thread mit niedrigster
 * Prioritaet, nicht Teil der Thread-Ueberwachung (SOLL-MQTT-01).
 */
#ifndef WALLFREE_APP_MQTT_TASK_H_
#define WALLFREE_APP_MQTT_TASK_H_

#include <wallfree/api/mqtt.h>

/** Nach netcfg_init() und http_api_start(). */
void mqtt_task_start(void);

/** mqtt_* geaendert: Verbindung mit den neuen Werten neu aufbauen (SOLL-MQTT-31). */
void mqtt_task_reconfigure(void);

/** Momentaufnahme des Status (SOLL-MQTT-30). */
void mqtt_task_status(struct wf_mq_status *out);

#endif
