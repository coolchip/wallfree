/* SPDX-License-Identifier: Apache-2.0 */
/* Uhrzeit per SNTP (SOLL-TIME-01..04), siehe timesync.c */
#ifndef WALLFREE_TIMESYNC_H_
#define WALLFREE_TIMESYNC_H_

#include <stdint.h>
#include <wallfree/api/json.h>

/** Thread starten (nach netcfg_init) */
void timesync_start(void);

/** ntp_* geaendert: sofort neu abgleichen bzw. abschalten */
void timesync_notify(void);

/** Uhrzeit in ms seit 1970, 0 = unbekannt */
int64_t timesync_now_ms(void);

/** JSON-Objekt fuer /system/info: {"time_ms", "synced_ms", "server"} bzw. null;
 *  Zeit aus der RTC: synced_ms null, server leer */
void timesync_json(struct wf_jbuf *b);

#endif /* WALLFREE_TIMESYNC_H_ */
