/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Verlauf (SOLL-API-33): Abtastung je Sekunde auf der Systemworkqueue,
 * Ringpuffer nur im RAM.
 */
#ifndef WALLFREE_APP_HISTORY_TASK_H_
#define WALLFREE_APP_HISTORY_TASK_H_

#include <stdint.h>
#include <wallfree/api/json.h>

void history_task_start(void);

/** wie wf_hist_json, mit Sperre */
uint32_t history_task_json(uint32_t cursor, struct wf_jbuf *b);

#endif
