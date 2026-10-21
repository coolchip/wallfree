/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zaehlerthread, siehe meter_task.h. Die Werte dienen nur der Anzeige
 * (F28): kein Pfad in Ladelogik oder Schutz, ein Ausfall erscheint nur im
 * Zaehlerstatus und als Ereignis. Nur gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "evse_task.h"
#include "meter_bus.h"
#include "meter_task.h"

LOG_MODULE_REGISTER(meter, LOG_LEVEL_INF);

#define METER_STACK 1280
#define METER_PRIO  K_LOWEST_APPLICATION_THREAD_PRIO

static struct meter mtr;
static uint32_t cycles;
static K_MUTEX_DEFINE(mtx);

static void bus_sleep(void *ctx, uint32_t ms)
{
	ARG_UNUSED(ctx);
	k_msleep(ms);
}

static int64_t bus_now(void *ctx)
{
	ARG_UNUSED(ctx);
	return k_uptime_get();
}

/* Der Bus laeuft ohne Sperre; nur Ergebnis und Zaehler werden geschuetzt. */
static void meter_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	static struct meter work;

	for (;;) {
		int64_t start = k_uptime_get();

		k_mutex_lock(&mtx, K_FOREVER);
		work = mtr;
		k_mutex_unlock(&mtx);
		uint32_t period = meter_cycle(&work);
		uint8_t ev = meter_take_events(&work);

		k_mutex_lock(&mtx, K_FOREVER);
		mtr = work;
		cycles++;
		k_mutex_unlock(&mtx);
		if (ev != 0) {
			/* SOLL-MTR-13/14: Ereignis an die Schnittstellen */
			evse_task_event_push((struct wf_event){
				.t = k_uptime_get(), .type = WF_EV_METER, .a = work.status,
				.b = (ev & METER_EV_ENERGY_DOWN) ? 1 : 0});
		}
		int64_t wait = start + period - k_uptime_get();

		if (wait > 0) {
			k_msleep((int32_t)wait);
		}
	}
}

K_THREAD_STACK_DEFINE(meter_stack, METER_STACK);
static struct k_thread meter_thread;

void meter_task_start(const struct wf_sysconf *c)
{
	const struct meter_bus bus = {meter_bus_read, bus_sleep, bus_now, NULL};

	meter_init(&mtr, c->meter_type, c->meter_addr, &bus);
	if (mtr.type == METER_NONE) {
		return; /* SOLL-MTR-01: nichts senden, PC12 bleibt Low */
	}
	LOG_INF("Zaehler %s, Adresse %u, %u Bd, Format %u", meter_type_name(mtr.type),
		c->meter_addr, c->meter_baud, c->meter_framing);
	if (meter_bus_init(c) != 0) {
		/* Thread laeuft trotzdem: Fehler erscheinen im Zaehlerstatus */
		LOG_ERR("Zaehlerbus nicht eingerichtet");
	}
	k_thread_create(&meter_thread, meter_stack, K_THREAD_STACK_SIZEOF(meter_stack),
			meter_entry, NULL, NULL, NULL, METER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&meter_thread, "meter");
}

uint8_t meter_task_type(void)
{
	return mtr.type; /* nach dem Start unveraenderlich */
}

uint32_t meter_task_get(struct meter_values *out)
{
	k_mutex_lock(&mtx, K_FOREVER);
	meter_values(&mtr, k_uptime_get(), out);
	uint32_t n = cycles;

	k_mutex_unlock(&mtx);
	return n;
}
