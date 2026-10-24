/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Parameterspeicher der Anwendung (SOLL-NVM-01..05/10, SOLL-RT-13).
 *
 *  - Sektor 7 ueber die Partition storage_partition; Sektoren 0..3 werden
 *    nie beschrieben (Pruefung beim Bauen, Sektor 3 ohne Schreibfunktion).
 *  - Anhaengen im Thread des Aufrufers (Shell/HTTP) bzw. im Speicher-Thread,
 *    nie im Automaten- oder Schutzthread.
 *  - Loeschen (Verdichten, Werksreset) nur in evse_flash_erase_allowed():
 *    die Ladelogik ist dabei gesperrt, die Thread-Ueberwachung ausgesetzt
 *    und das IWDG-Limit verlaengert. Waehrend des Loeschens steht die CPU
 *    (Code im Flash, eine Bank); Schuetz ist aus, CP liegt auf +12 V.
 *
 * Nur gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include "evse_task.h"
#include "hw.h"
#include "storage.h"
#include <wallfree/nvm/orig.h>

LOG_MODULE_REGISTER(storage, LOG_LEVEL_INF);

/* SOLL-NVM-01: eigener Bereich genau Sektor 7, Original genau Sektor 3 */
BUILD_ASSERT(PARTITION_OFFSET(storage_partition) == 0x60000 &&
	     PARTITION_SIZE(storage_partition) == 0x20000, "Parameterbereich muss Sektor 7 sein");
BUILD_ASSERT(PARTITION_OFFSET(orig_params_partition) == 0xC000 &&
	     PARTITION_SIZE(orig_params_partition) == 0x4000, "Originalbereich muss Sektor 3 sein");

#define STORAGE_PRIO       12
#define STORAGE_STACK      1536
#define STORAGE_PERIOD_MS  1000
#define COMPACT_RESERVE    4096 /* ab hier im naechsten sicheren Zustand verdichten */

static const struct flash_area *fa_store;
static const struct flash_area *fa_orig;
static struct nvm_flash fl_store;
static struct nvm_flash fl_orig;
static struct nvm nvm;
static K_MUTEX_DEFINE(nvm_mtx); /* Reihenfolge: zuerst evse_acquire(), dann nvm_mtx */
static uint8_t scratch[2048];

static bool invalid_at_boot;
static uint32_t compactions;
static int last_error;
static uint32_t flash_sr_boot; /* FLASH_SR-Fehlerbits beim Start */
static atomic_t lock_pending; /* 0 = nichts, sonst Zustand + 1 */

K_THREAD_STACK_DEFINE(storage_stack, STORAGE_STACK);
static struct k_thread storage_thread;
static K_SEM_DEFINE(storage_sem, 0, 1);

/* ------------------------------------------------ Flash-Zugriff */

static int fa_read(void *ctx, uint32_t off, void *buf, size_t len)
{
	return flash_area_read(ctx, off, buf, len);
}

static int fa_write(void *ctx, uint32_t off, const void *buf, size_t len)
{
	return flash_area_write(ctx, off, buf, len);
}

static int fa_erase(void *ctx)
{
	const struct flash_area *fa = ctx;

	return flash_area_erase(fa, 0, fa->fa_size);
}

static int ro_write(void *ctx, uint32_t off, const void *buf, size_t len)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(off);
	ARG_UNUSED(buf);
	ARG_UNUSED(len);
	return -EROFS; /* Sektor 3 wird nie beschrieben */
}

static int ro_erase(void *ctx)
{
	ARG_UNUSED(ctx);
	return -EROFS;
}

/* Loeschvorgang mit verlaengertem IWDG (Ladelogik ist dabei angehalten oder noch nicht gestartet) */
static int erase_op(int (*op)(void))
{
	hw_iwdg_long(true);
	int r = op();

	hw_iwdg_long(false);
	return r;
}

static int do_compact(void)
{
	return nvm_compact(&nvm, scratch, sizeof(scratch));
}

static int do_format(void)
{
	return nvm_format(&nvm);
}

/* ------------------------------------------------ Start */

int storage_init(void)
{
	int r = flash_area_open(PARTITION_ID(storage_partition), &fa_store);

	if (r == 0) {
		r = flash_area_open(PARTITION_ID(orig_params_partition), &fa_orig);
	}
	if (r != 0) {
		LOG_ERR("Flash-Bereiche nicht verfuegbar (%d), nur Werkseinstellungen", r);
		return r;
	}
	fl_store = (struct nvm_flash){.read = fa_read, .write = fa_write, .erase = fa_erase,
				      .ctx = (void *)fa_store, .size = fa_store->fa_size};
	fl_orig = (struct nvm_flash){.read = fa_read, .write = ro_write, .erase = ro_erase,
				     .ctx = (void *)fa_orig, .size = fa_orig->fa_size};

	flash_sr_boot = hw_flash_take_errors();
	if (flash_sr_boot != 0) {
		LOG_WRN("FLASH_SR-Fehlerbits beim Start: 0x%02x (geloescht)", flash_sr_boot);
	}
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	r = nvm_mount(&nvm, &fl_store);
	if (r == NVM_OK && nvm.state == NVM_ST_INVALID) {
		/* SOLL-NVM-05: Werkseinstellungen und Meldung; beim Start ist Loeschen erlaubt */
		invalid_at_boot = true;
		LOG_WRN("Parameterbereich ungueltig (Verdichten unterbrochen?): Werkseinstellungen");
		r = erase_op(do_format);
	}
	k_mutex_unlock(&nvm_mtx);
	if (r != NVM_OK) {
		last_error = r;
		LOG_ERR("Parameterspeicher: Fehler %d", r);
	}
	return r;
}

int storage_factory_reset_boot(void)
{
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	int r = nvm.state == NVM_ST_UNMOUNTED ? NVM_E_STATE : erase_op(do_format);

	k_mutex_unlock(&nvm_mtx);
	return r;
}

int storage_load(struct evse_config *cfg, enum evse_lock_state *lock)
{
	struct evse_load_result res;

	k_mutex_lock(&nvm_mtx, K_FOREVER);
	int r = nvm.state == NVM_ST_UNMOUNTED ? NVM_E_STATE : evse_persist_load(&nvm, cfg, lock, &res);

	k_mutex_unlock(&nvm_mtx);
	if (r == NVM_OK) {
		LOG_INF("Parameter: %u geladen, %u abgelehnt, %u unbekannt", res.restored,
			res.rejected, res.unknown);
	}
	return r;
}

/* ------------------------------------------------ Betrieb */

static void note(int r)
{
	if (r < 0) {
		last_error = r;
	}
}

int storage_save_param(const struct evse_param *p, int32_t v)
{
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	int r = nvm.state == NVM_ST_UNMOUNTED ? NVM_E_STATE : evse_persist_param(&nvm, p, v);

	note(r);
	k_mutex_unlock(&nvm_mtx);
	if (r == NVM_PENDING) {
		k_sem_give(&storage_sem); /* moeglichst bald verdichten */
	}
	return r;
}

void storage_lock_changed(enum evse_lock_state st)
{
	if (st == EVSE_LOCK_LOCKED || st == EVSE_LOCK_UNLOCKED) {
		atomic_set(&lock_pending, (atomic_val_t)st + 1);
		k_sem_give(&storage_sem);
	}
}

/* Loeschen im sicheren Zustand; Ladelogik waehrenddessen gesperrt */
static int erase_when_safe(int (*op)(void))
{
	struct evse *e = evse_acquire();
	int r;

	if (!evse_flash_erase_allowed(e)) {
		evse_release();
		return -EBUSY;
	}
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	evse_task_maint_begin();
	r = erase_op(op);
	evse_task_maint_end();
	note(r);
	k_mutex_unlock(&nvm_mtx);
	evse_release();
	return r;
}

static void storage_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		(void)k_sem_take(&storage_sem, K_MSEC(STORAGE_PERIOD_MS));
		evse_task_iface_beat(EVSE_IFACE_STORAGE);

		atomic_val_t lp = atomic_set(&lock_pending, 0);

		if (lp != 0) {
			k_mutex_lock(&nvm_mtx, K_FOREVER);
			note(evse_persist_lock(&nvm, (enum evse_lock_state)(lp - 1)));
			k_mutex_unlock(&nvm_mtx);
		}

		k_mutex_lock(&nvm_mtx, K_FOREVER);
		bool need = nvm.state != NVM_ST_UNMOUNTED && nvm_needs_compact(&nvm, COMPACT_RESERVE);

		k_mutex_unlock(&nvm_mtx);
		if (need) {
			int r = erase_when_safe(do_compact);

			if (r == NVM_OK) {
				compactions++;
				LOG_INF("Parameterbereich verdichtet");
			} else if (r != -EBUSY) {
				LOG_ERR("Verdichten fehlgeschlagen: %d", r);
			}
		}
	}
}

void storage_start(void)
{
	k_thread_create(&storage_thread, storage_stack, K_THREAD_STACK_SIZEOF(storage_stack),
			storage_entry, NULL, NULL, NULL, STORAGE_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&storage_thread, "storage");
}

int storage_factory_reset(void)
{
	return erase_when_safe(do_format);
}

int storage_import_orig(evse_import_report_t report, void *ctx)
{
	if (fa_orig == NULL) {
		return NVM_E_STATE;
	}
	struct evse *e = evse_acquire();

	if (!evse_flash_erase_allowed(e)) {
		evse_release();
		return EVSE_IMP_E_UNSAFE;
	}
	/* viele Suchlaeufe ueber 16 KB bei gesperrter Ladelogik: Ueberwachung aussetzen */
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	evse_task_maint_begin();
	int r = evse_import_orig(e, &fl_orig, &nvm, k_uptime_get(), report, ctx);

	evse_task_maint_end();
	note(r < 0 ? r : 0);
	k_mutex_unlock(&nvm_mtx);
	evse_release();
	return r;
}

void storage_get_status(struct storage_status *st)
{
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	st->state = nvm.state;
	st->used = nvm_used(&nvm);
	st->free = nvm.state == NVM_ST_UNMOUNTED ? 0 : nvm_free(&nvm);
	st->pending = nvm.npend;
	st->damaged = nvm.damaged;
	st->invalid_at_boot = invalid_at_boot;
	st->compactions = compactions;
	st->last_error = last_error;
	st->flash_sr_boot = flash_sr_boot;
	k_mutex_unlock(&nvm_mtx);
}

int storage_read_raw(uint16_t id, void *buf, size_t max)
{
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	int r = nvm.state == NVM_ST_UNMOUNTED ? NVM_E_STATE : nvm_read(&nvm, id, buf, max);

	k_mutex_unlock(&nvm_mtx);
	return r;
}

int storage_save_raw(uint16_t id, const void *data, size_t len)
{
	k_mutex_lock(&nvm_mtx, K_FOREVER);
	int r = nvm.state == NVM_ST_UNMOUNTED ? NVM_E_STATE : nvm_write(&nvm, id, data, len);

	note(r);
	k_mutex_unlock(&nvm_mtx);
	if (r == NVM_PENDING) {
		k_sem_give(&storage_sem);
	}
	return r;
}

int storage_orig_lookup(void *ctx, uint16_t field, uint16_t *value)
{
	ARG_UNUSED(ctx);
	if (fa_orig == NULL) {
		return -1;
	}
	return nvm_orig_read(&fl_orig, field, value) == NVM_OK ? 0 : -1;
}
