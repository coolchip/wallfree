/* SPDX-License-Identifier: Apache-2.0 */
/* Verlauf (SOLL-API-33) */
#include <zephyr/kernel.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/api/history.h>
#include <wallfree/evse/charger.h>
#include "evse_task.h"
#include "history_task.h"
#include "meter_task.h"

#define SAMPLE_MS 1000

static struct wf_hist hist;
static K_MUTEX_DEFINE(hist_mtx);

static void sample_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(sample_work, sample_fn);

static void sample_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	static struct meter_values mv; /* nicht auf dem Stack der Systemworkqueue */
	int32_t power = -1;
	uint8_t flags = 0;
	struct evse *e = evse_acquire();
	int iec = api_iec61851_state(e);
	int cs = api_charger_state(e);
	int32_t allowed = evse_allowed_current(e);

	if (api_error_state(e) != 0 || cs == 4) {
		flags |= WF_HIST_ERROR;
	}
	evse_release();
	if (iec >= 1 && iec <= 3) {
		flags |= WF_HIST_CONNECTED;
	}
	if (cs == 3) {
		flags |= WF_HIST_CHARGING;
	}
	if (meter_task_type() != METER_NONE) {
		(void)meter_task_get(&mv);
		if (mv.valid[METER_P]) {
			/* mW; Einspeisung an der Ladestation zaehlt als 0 */
			power = mv.v[METER_P] > 0 ? (int32_t)(mv.v[METER_P] / 1000) : 0;
		}
	}
	k_mutex_lock(&hist_mtx, K_FOREVER);
	wf_hist_add(&hist, k_uptime_get(), power, allowed, flags);
	k_mutex_unlock(&hist_mtx);
	k_work_schedule(&sample_work, K_MSEC(SAMPLE_MS));
}

void history_task_start(void)
{
	wf_hist_init(&hist);
	k_work_schedule(&sample_work, K_NO_WAIT);
}

uint32_t history_task_json(uint32_t cursor, struct wf_jbuf *b)
{
	k_mutex_lock(&hist_mtx, K_FOREVER);
	uint32_t next = wf_hist_json(&hist, k_uptime_get(), cursor, b);

	k_mutex_unlock(&hist_mtx);
	return next;
}
