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
#include <zephyr/fatal.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/logging/log_ctrl.h>
#include <zephyr/sys/atomic.h>
#include <wallfree/api/events.h>
#include "evse_task.h"
#include "http_api.h"
#include "hw.h"
#include "storage.h"

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
static atomic_t maint;          /* Ueberwachung ausgesetzt (Flash-Loeschen) */

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

/* ------------------------------------------------ geordneter Neustart (SOLL-RT-22) */

#define REBOOT_WAIT_MS 15000

static int64_t reboot_deadline;
static atomic_t reboot_pending;
static enum wf_reset reboot_code;
static bool handover;           /* statt Reset an den Bootloader (SOLL-BOOT-30) */
static uint32_t handover_ip, handover_mask;

static void reboot_work_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(reboot_work, reboot_work_fn);

/*
 * Kein LOG_PANIC: ein Shell-Log-Backend (frueher Telnet) bricht dabei seine Sende-Arbeit
 * mit k_work_cancel_delayable_sync() auf der Systemworkqueue ab, in der dieser Ablauf
 * selbst laeuft (Selbstblockade, gefunden im PC-Test). Stattdessen bekommt der
 * Log-Thread nach der Meldung REBOOT_LOG_MS zum Ausgeben.
 */
#define REBOOT_LOG_MS 200

static bool reboot_logged;

static void reboot_work_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	if (reboot_logged) {
		/* beide zuerst sicherer Zustand */
		if (handover) {
			hw_bootloader_handover(handover_ip, handover_mask);
		}
		hw_reset_with(reboot_code);
	}
	struct evse *e = evse_acquire();
	bool off = evse_contactor_is_off(e);

	evse_release();
	if (off || k_uptime_get() >= reboot_deadline) {
		if (!off) {
			/* Schuetz sofort aus, nicht erst nach der Log-Wartezeit */
			hw_safe_state_regs();
		}
		LOG_WRN("%s%s", handover ? "Uebergabe an den Bootloader" : "Neustart",
			off ? "" : " nach Fristablauf (Schuetz hart abgeschaltet)");
		reboot_logged = true;
		k_work_schedule(&reboot_work, K_MSEC(REBOOT_LOG_MS));
		return;
	}
	k_work_schedule(&reboot_work, K_MSEC(100));
}

static int orderly(enum wf_reset code, bool to_bootloader, uint32_t ip, uint32_t mask)
{
	if (!atomic_cas(&reboot_pending, 0, 1)) {
		return -EALREADY;
	}
	reboot_code = code;
	handover = to_bootloader;
	handover_ip = ip;
	handover_mask = mask;
	evse_request_shutdown(evse_acquire());
	evse_release();
	reboot_deadline = k_uptime_get() + REBOOT_WAIT_MS;
	/* kurz warten, damit Antworten (HTTP, Shell) noch hinausgehen */
	k_work_schedule(&reboot_work, K_MSEC(300));
	return 0;
}

int evse_task_reboot_orderly(enum wf_reset code)
{
	return orderly(code, false, 0, 0);
}

int evse_task_update_orderly(uint32_t ip, uint32_t mask)
{
	return orderly(WF_RST_UPDATE, true, ip, mask);
}

void evse_task_maint_begin(void)
{
	atomic_set(&maint, 1);
}

void evse_task_maint_end(void)
{
	int64_t now = k_uptime_get();

	atomic_set(&charger_beat, (atomic_val_t)now);
	atomic_set(&block_beat, (atomic_val_t)now);
	atomic_set(&maint, 0);
}

/* ------------------------------------------------ sicherer Zustand und Neustart */

static void fail_safe_and_reboot(const char *why, enum wf_reset_detail detail)
{
	hw_safe_state_regs();
	hw_reset_detail(detail); /* SOLL-RT-14: welche Ueberwachung, steht nach dem Start im Protokoll */
	/* ohne LOG_PANIC (siehe reboot_work_fn); die Meldung geht evtl. verloren,
	 * der Grund steht im Backup-Register (SOLL-RT-14) */
	LOG_ERR("sicherer Zustand: %s", why);
	hw_reset_with(WF_RST_LIMIT);
}

void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf)
{
	/* SOLL-RT-21: zuerst Schuetz aus und CP -12 V, dann Neustart */
	hw_safe_state_regs();
#ifdef CONFIG_ARM
	hw_fault_note(reason, esf != NULL ? esf->basic.pc : 0);
#else
	ARG_UNUSED(esf);
	hw_fault_note(reason, 0);
#endif
	LOG_PANIC();
	LOG_ERR("Fataler Fehler %u, Neustart", reason);
	hw_reset_with(WF_RST_FAULT);
}

/* ------------------------------------------------ Ereignisse (SOLL-ERR-10) */

static struct wf_ev_ring ev_ring;
static struct k_spinlock ev_lock;

static void events_post(const struct wf_ev_snap *before, const struct wf_ev_snap *after,
			int64_t now)
{
	struct wf_event ev[WF_EV_MAX_DIFF];
	size_t n = wf_ev_diff(before, after, now, ev);

	if (n == 0) {
		return;
	}
	k_spinlock_key_t key = k_spin_lock(&ev_lock);

	for (size_t i = 0; i < n; i++) {
		wf_ev_ring_push(&ev_ring, ev[i]);
	}
	k_spin_unlock(&ev_lock, key);
	http_api_notify(); /* Ausgabe und Log im WebSocket-Thread */
}

void evse_task_event_push(struct wf_event ev)
{
	k_spinlock_key_t key = k_spin_lock(&ev_lock);

	wf_ev_ring_push(&ev_ring, ev);
	k_spin_unlock(&ev_lock, key);
	http_api_notify();
}

bool evse_task_event_get(uint32_t *seq, struct wf_event *ev)
{
	k_spinlock_key_t key = k_spin_lock(&ev_lock);
	bool ok = wf_ev_ring_get(&ev_ring, seq, ev);

	k_spin_unlock(&ev_lock, key);
	return ok;
}

/* ------------------------------------------------ Automatenthread */

static void charger_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	uint32_t last_seq = 0;
	enum evse_lock_state last_lock = EVSE_LOCK_INIT;

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

		struct wf_ev_snap snap_before, snap_after;

		k_mutex_lock(&ev_mtx, K_FOREVER);
		wf_ev_snap_take(&ev, &snap_before);
		evse_step(&ev, &in, now);
		wf_ev_snap_take(&ev, &snap_after);
		out = ev.out;
		bool p_errors = (ev.errors & EVSE_ERR_CLASS_P) != 0;
		enum evse_lock_state lock = ev.lock.state;
		k_mutex_unlock(&ev_mtx);

		events_post(&snap_before, &snap_after, now);

		/* Feld 465: Endzustand speichern lassen (Speicher-Thread, SOLL-PAR-40) */
		if (lock != last_lock) {
			last_lock = lock;
			storage_lock_changed(lock);
		}

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

/* ------------------------------------------------ IWDG (SOLL-BOOT-03, SOLL-RT-12) */

static atomic_t wdt_ok;

int evse_task_wdt_start(void)
{
	int r = hw_wdt_start(IWDG_TIMEOUT_MS);

	atomic_set(&wdt_ok, r == 0);
	return r;
}

void evse_task_wdt_feed(void)
{
	/* nur bis der Schutzthread laeuft; danach bedient ausschliesslich er */
	if (atomic_get(&wdt_ok) && !atomic_get(&started)) {
		hw_wdt_feed();
	}
}

/* ------------------------------------------------ Schnittstellen-Threads (SOLL-RT-11) */

#define IFACE_LIMIT_MS 10000

static atomic_t iface_beat[EVSE_IFACE_COUNT];
static const char *const iface_names[EVSE_IFACE_COUNT] = {"storage", "ws_push", "sysworkq",
								  "http_server"};
static const enum wf_reset_detail iface_detail[EVSE_IFACE_COUNT] = {
	WF_RSTD_IFACE_STORAGE, WF_RSTD_IFACE_WS, WF_RSTD_IFACE_SYSWORKQ, WF_RSTD_IFACE_HTTP};
static atomic_t http_busy; /* HTTP-Server steckt in einem Aufruf der Anwendung */

void evse_task_iface_beat(enum evse_iface id)
{
	atomic_set(&iface_beat[id], (atomic_val_t)k_uptime_get_32());
}

/* HTTP-Server: meldet sich nicht zyklisch; ueberwacht wird die Dauer jedes Aufrufs */
void evse_task_http_busy(bool busy)
{
	if (busy) {
		evse_task_iface_beat(EVSE_IFACE_HTTP);
	}
	atomic_set(&http_busy, busy);
}

static void sysworkq_beat_fn(struct k_work *w);
static K_WORK_DELAYABLE_DEFINE(sysworkq_beat, sysworkq_beat_fn);

static void sysworkq_beat_fn(struct k_work *w)
{
	ARG_UNUSED(w);
	evse_task_iface_beat(EVSE_IFACE_SYSWORKQ);
	k_work_schedule(&sysworkq_beat, K_SECONDS(1));
}

/*
 * Einmal je Sekunde aus dem Schutzthread: haengt ein Schnittstellen-Thread
 * laenger als 10 s, Meldung; Neustart nur ohne Ladevorgang (Schuetz aus,
 * S1/S9/S10). Die Sperre der Ladelogik wird nur ohne Warten versucht.
 */
static void check_ifaces(uint32_t now32)
{
	static bool reported;
	int hung = -1;

	for (int i = 0; i < EVSE_IFACE_COUNT; i++) {
		if (i == EVSE_IFACE_HTTP && !atomic_get(&http_busy)) {
			continue;
		}
		if (now32 - (uint32_t)atomic_get(&iface_beat[i]) > IFACE_LIMIT_MS) {
			hung = i;
		}
	}
	if (hung < 0) {
		reported = false;
		return;
	}
	if (!reported) {
		reported = true;
		LOG_ERR("Thread %s meldet sich nicht (> %d s)", iface_names[hung], IFACE_LIMIT_MS / 1000);
	}
	if (k_mutex_lock(&ev_mtx, K_NO_WAIT) != 0) {
		return;
	}
	bool idle = evse_contactor_is_off(&ev) &&
		    (ev.state == EVSE_S1_IDLE || ev.state == EVSE_S9_ERROR ||
		     ev.state == EVSE_S10_UNAVAILABLE);

	k_mutex_unlock(&ev_mtx);
	if (idle) {
		hw_reset_detail(iface_detail[hung]);
		hw_reset_with(WF_RST_NET_HANG);
	}
}

/* ------------------------------------------------ Schutzthread */

static void prot_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);
	if (!atomic_get(&wdt_ok)) {
		fail_safe_and_reboot("IWDG nicht verfuegbar", WF_RSTD_NONE);
	}
	int64_t t0 = k_uptime_get();
	int64_t last_iface_check = t0;

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
		if (now - t0 > STARTUP_GRACE_MS && !atomic_get(&maint)) {
			if (now32 - (uint32_t)atomic_get(&charger_beat) > LIMIT_CHARGER_MS) {
				fail_safe_and_reboot("Automatenthread haengt", WF_RSTD_CHARGER);
			}
			if (now32 - (uint32_t)atomic_get(&block_beat) > LIMIT_BLOCK_MS) {
				fail_safe_and_reboot("keine Messbloecke", WF_RSTD_BLOCKS);
			}
		}
		/* SOLL-RT-11: Schnittstellen-Threads, einmal je Sekunde */
		if (now - t0 > STARTUP_GRACE_MS && now - last_iface_check >= 1000) {
			last_iface_check = now;
			check_ifaces(now32);
		}
		/* SOLL-RT-12: IWDG nur bei eingehaltenen Limits */
		hw_wdt_feed();
		k_sleep(K_MSEC(1));
	}
}

int evse_task_start(const struct evse_config *cfg, enum evse_lock_state stored_lock)
{
	if (atomic_cas(&started, 0, 1) == false) {
		return -EALREADY;
	}
	int64_t now = k_uptime_get();

	evse_init(&ev, cfg, stored_lock, now);
	wf_ev_ring_init(&ev_ring);
	atomic_set(&charger_beat, (atomic_val_t)now);
	atomic_set(&block_beat, (atomic_val_t)now);
	for (int i = 0; i < EVSE_IFACE_COUNT; i++) {
		evse_task_iface_beat(i);
	}
	k_work_schedule(&sysworkq_beat, K_SECONDS(1));

	k_thread_create(&prot_thread, prot_stack, K_THREAD_STACK_SIZEOF(prot_stack), prot_entry,
			NULL, NULL, NULL, PROT_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&prot_thread, "evse_prot");
	k_thread_create(&charger_thread, charger_stack, K_THREAD_STACK_SIZEOF(charger_stack),
			charger_entry, NULL, NULL, NULL, CHARGER_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&charger_thread, "evse_logic");
	return 0;
}
