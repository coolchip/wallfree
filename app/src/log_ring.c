/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Log-Backend in einen Ring im RAM (SOLL-SH-22): die juengsten Meldungen, lesbar mit
 * evse log in der Konsole. Zugleich das einzige Log-Backend der Steuerung; Zephyrs
 * Log-Thread verlangt mindestens eines (ohne: Assertion beim Start, Startschleife).
 * Der Ring ueberlebt keinen Neustart.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log_backend.h>
#include <zephyr/logging/log_output.h>
#include <zephyr/shell/shell.h>
#include "log_ring.h"

#define RING_SIZE 2048 /* Zweierpotenz */
#define SHOW_MAX  1900 /* passt mit Kopfzeile in die 2 KB der Konsole */

static char ring[RING_SIZE];
static uint32_t head; /* Zahl der je geschriebenen Zeichen */
static struct k_spinlock lock;
static uint8_t out_buf[64];

static int char_out(uint8_t *data, size_t length, void *ctx)
{
	ARG_UNUSED(ctx);
	k_spinlock_key_t key = k_spin_lock(&lock);

	for (size_t i = 0; i < length; i++) {
		if (data[i] != '\r') {
			ring[head++ & (RING_SIZE - 1)] = (char)data[i];
		}
	}
	k_spin_unlock(&lock, key);
	return (int)length;
}

LOG_OUTPUT_DEFINE(log_ring_output, char_out, out_buf, sizeof(out_buf));

static void process(const struct log_backend *const backend, union log_msg_generic *msg)
{
	ARG_UNUSED(backend);
	log_output_msg_process(&log_ring_output, &msg->log,
			       LOG_OUTPUT_FLAG_TIMESTAMP | LOG_OUTPUT_FLAG_FORMAT_TIMESTAMP |
				       LOG_OUTPUT_FLAG_LEVEL | LOG_OUTPUT_FLAG_CRLF_LFONLY);
}

static void panic(const struct log_backend *const backend)
{
	ARG_UNUSED(backend);
	log_output_flush(&log_ring_output);
}

static void dropped(const struct log_backend *const backend, uint32_t cnt)
{
	ARG_UNUSED(backend);
	log_output_dropped_process(&log_ring_output, cnt);
}

static const struct log_backend_api log_ring_api = {
	.process = process,
	.panic = panic,
	.dropped = dropped,
};

LOG_BACKEND_DEFINE(log_ring_backend, log_ring_api, true);

void log_ring_print(const struct shell *sh)
{
	char chunk[64];
	k_spinlock_key_t key = k_spin_lock(&lock);
	uint32_t end = head;
	uint32_t pos = end > SHOW_MAX ? end - SHOW_MAX : 0;

	k_spin_unlock(&lock, key);
	bool skip = pos > 0; /* angeschnittene erste Zeile weglassen */

	while (pos < end) {
		size_t n = 0;

		key = k_spin_lock(&lock);
		if (head - pos > RING_SIZE) {
			pos = head - SHOW_MAX; /* waehrenddessen ueberschrieben */
			skip = true;
		}
		while (n < sizeof(chunk) && pos < end) {
			chunk[n++] = ring[pos++ & (RING_SIZE - 1)];
		}
		k_spin_unlock(&lock, key);

		size_t i = 0;

		while (skip && i < n) {
			skip = chunk[i++] != '\n';
		}
		if (i < n) {
			shell_fprintf(sh, SHELL_NORMAL, "%.*s", (int)(n - i), chunk + i);
		}
	}
}

void log_ring_clear(void)
{
	k_spinlock_key_t key = k_spin_lock(&lock);

	head = 0;
	k_spin_unlock(&lock, key);
}
