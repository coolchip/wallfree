/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zaehlerthread (Soll-Spezifikation 12.5): niedrigste Prioritaet, nur mit
 * konfiguriertem Zaehler, nicht in der Thread-Ueberwachung (SOLL-MTR-03).
 */
#ifndef WALLFREE_APP_METER_TASK_H_
#define WALLFREE_APP_METER_TASK_H_

#include <wallfree/api/sysconf.h>
#include <wallfree/meter/meter.h>

/** Mit der beim Start angewandten Konfiguration; ohne Zaehler nichts. */
void meter_task_start(const struct wf_sysconf *c);

/** Zaehlertyp der laufenden Konfiguration (0 = keiner) */
uint8_t meter_task_type(void);

/** Momentaufnahme; @return Zaehler der abgeschlossenen Zyklen (fuer WebSocket) */
uint32_t meter_task_get(struct meter_values *out);

#endif
