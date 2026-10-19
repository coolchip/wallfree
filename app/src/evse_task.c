/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Ablaufeinheiten (Soll-Spezifikation Abschnitt 3):
 *  - Schutzthread (hoechste Thread-Prioritaet, 1 ms): RCM-Pegel zyklisch
 *    pruefen, Threads ueberwachen, IWDG bedienen (SOLL-RT-10..12,
 *    SOLL-PROT-05 Rueckfallebene).
 *  - Automatenthread (10 ms und bei jedem Messblock): evse_step(),
 *    Ausgaben setzen.
 *  - Fault-Handler: sicherer Zustand per Register, dann Neustart (SOLL-RT-21).
 *
 * Nur gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <zephyr/drivers/watchdog.h>
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include "evse_task.h"
#include "hw.h"

LOG_MODULE_REGISTER(evse_task, LOG_LEVEL_INF);

#define PROT_PRIO      0   /* hoechste preemptive Prioritaet */
#define CHARGER_PRIO   2
#define PROT_STACK     1024
#define CHARGER_STACK  2048

#define CHARGER_CYCLE_MS 10   /* SOLL-RT-05 */
#define LIMIT_CHARGER_MS 100  /* SOLL-RT-10 (F4) */
#define LIMIT_BLOCK_MS   50
#define IWDG_TIMEOUT_MS  250  /* SOLL-RT-12 */
#define STARTUP_GRACE_MS 1000

static struct evse ev;
static K_MUTEX_DEFINE(ev_mtx);

static atomic_t charger_beat;   /* Zeitstempel ms (untere 32 Bit) */
static atomic_t block_beat;
static atomic_t started;

K_THREAD_STACK_DEFINE(prot_stack, PROT_STACK);
K_THREAD_STACK_DEFINE(charger_stack, CHARGER_STACK);
static struct k_thread prot_thread;
static struct k_thread charger_thread;

struct evse *evse_acquire(void)
{
	k_mutex_lock(&ev_mtx, K_FOREVER);
	return &ev;
}

void evse_release(void)
{
	k_mutex_unlock(&ev_mtx);
}

/* ------------------------------------------------ sicherer Zustand und Neustart */

static void fail_safe_and_reboot(const char *why)
{
	hw_safe_state_regs();
	LOG_ERR("sicherer Zustand: %s", why);
	LOG_PANIC();
	sys_reboot(SYS_REBOOT_COLD);
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	ARG_UNUSED(esf);
	/* SOLL-RT-21: zuerst Schuetz aus und CP -12 V, dann Neustart */
	hw_safe_state_regs();
	LOG_PANIC();
	LOG_ERR("Fataler Fehler %u, Neustart", reason);
	sys_reboot(SYS_REBOOT_COLD);
	CODE_UNREACHABLE;
}

/* ------------------------------------------------ Automatenthread */

static void charger_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	uint32_t last_seq = 0;

	for (;;) {
		struct evse_inputs in = {0};
		struct hw_block blk;
		struct evse_outputs out;

		(void)k_sem_take(&hw_block_sem, K_MSEC(CHARGER_CYCLE_MS));
		int64_t now = k_uptime_get();

		if (hw_get_block(&blk) && blk.seq != last_seq) {
			last_seq = blk.seq;
			in.new_block = true;
			atomic_set(&block_beat, (atomic_val_t)blk.at_ms);
		}
		in.cp_pos_mv = blk.cp_pos;
		in.cp_neg_mv = blk.cp_neg;
		in.pp_mv = blk.pp;
		for (int i = 0; i < EVSE_DIN_COUNT; i++) {
			in.din_mv[i] = blk.din[i];
		}
		in.supply_mv = blk.supply;
		hw_gpio_read_rcm(&in.rcm);
		in.last_sense_edge = hw_gpio_last_sense_edge();
		in.meter_valid = false; /* keine Messquelle (F1) */

		k_mutex_lock(&ev_mtx, K_FOREVER);
		evse_step(&ev, &in, now);
		out = ev.out;
		bool p_errors = (ev.errors & EVSE_ERR_CLASS_P) != 0;
		k_mutex_unlock(&ev_mtx);

		/* Schutz-Latch des ISR-Pfads erst loesen, wenn die Ladelogik den
		 * Fehler uebernommen hat bzw. keine Ausloesung mehr ansteht */
		if (hw_protect_latched() && !p_errors && !in.rcm.sw1 && !in.rcm.sw2 && !in.rcm.err &&
		    !out.contactor) {
			hw_protect_clear();
		}

		hw_cp_set_duty(out.cp_duty);
		hw_gpio_apply(&out);
		atomic_set(&charger_beat, (atomic_val_t)now);
	}
}

/* ------------------------------------------------ Schutzthread */

static void prot_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(iwdg));
	struct wdt_timeout_cfg wcfg = {
		.window = {.min = 0, .max = IWDG_TIMEOUT_MS},
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ch = -1;

	if (device_is_ready(wdt)) {
		ch = wdt_install_timeout(wdt, &wcfg);
		if (ch >= 0) {
			(void)wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
		}
	}
	if (ch < 0) {
		fail_safe_and_reboot("IWDG nicht verfuegbar");
	}
	int64_t t0 = k_uptime_get();

	for (;;) {
		struct evse_rcm_levels lv;
		int64_t now = k_uptime_get();
		uint32_t now32 = (uint32_t)now;

		/* SOLL-PROT-05 Rueckfallebene: zyklische Pegelkontrolle */
		hw_gpio_read_rcm(&lv);
		if (lv.sw1 || lv.sw2 || lv.err) {
			hw_protect_trip();
		}

		/* SOLL-RT-10: Thread- und Messueberwachung */
		if (now - t0 > STARTUP_GRACE_MS) {
			if (now32 - (uint32_t)atomic_get(&charger_beat) > LIMIT_CHARGER_MS) {
				fail_safe_and_reboot("Automatenthread haengt");
			}
			if (now32 - (uint32_t)atomic_get(&block_beat) > LIMIT_BLOCK_MS) {
				fail_safe_and_reboot("keine Messbloecke");
			}
		}
		/* SOLL-RT-12: IWDG nur bei eingehaltenen Limits */
		(void)wdt_feed(wdt, ch);
		k_sleep(K_MSEC(1));
	}
}

int evse_task_start(const struct evse_config *cfg)
{
	if (atomic_cas(&started, 0, 1) == false) {
		return -EALREADY;
	}
	int64_t now = k_uptime_get();

	evse_init(&ev, cfg, EVSE_LOCK_UNLOCKED /* NVM folgt (SOLL-NVM) */, now);
	atomic_set(&charger_beat, (atomic_val_t)now);
	atomic_set(&block_beat, (atomic_val_t)now);

	k_thread_create(&prot_thread, prot_stack, K_THREAD_STACK_SIZEOF(prot_stack), prot_entry,
			NULL, NULL, NULL, PROT_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&prot_thread, "evse_prot");
	k_thread_create(&charger_thread, charger_stack, K_THREAD_STACK_SIZEOF(charger_stack),
			charger_entry, NULL, NULL, NULL, CHARGER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&charger_thread, "evse_logic");
	return 0;
}
