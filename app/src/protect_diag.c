/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Diagnose des Schutzpfads (SOLL-PROT-15): je Leitung gezaehlt, wie der Schutzmerker
 * ausgeloest wurde bzw. welche Interrupts der Stoerfilter verworfen hat. Nur RAM.
 */
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include "hw.h"

static atomic_t cnt[HW_PROT_PATHS][HW_PROT_LINES];
static atomic_t last_line = ATOMIC_INIT(-1);
static atomic_t last_path;
static int64_t last_ms;

void hw_protect_note(enum hw_prot_line line, enum hw_prot_path path)
{
	atomic_inc(&cnt[path][line]);
	if (path != HW_PROT_ISR_IGNORED) {
		last_ms = k_uptime_get();
		atomic_set(&last_path, path);
		atomic_set(&last_line, line);
	}
}

void hw_protect_diag(struct hw_protect_diag *d)
{
	for (int p = 0; p < HW_PROT_PATHS; p++) {
		for (int l = 0; l < HW_PROT_LINES; l++) {
			d->count[p][l] = (uint32_t)atomic_get(&cnt[p][l]);
		}
	}
	d->last_line = (int)atomic_get(&last_line);
	d->last_path = (int)atomic_get(&last_path);
	d->last_ms = last_ms;
}
