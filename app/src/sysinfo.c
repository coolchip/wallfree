/* SPDX-License-Identifier: Apache-2.0 */
/* Systeminformationen, siehe sysinfo.h. Nur gebaut. */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "hw.h"
#include "sysinfo.h"

LOG_MODULE_REGISTER(sysinfo, LOG_LEVEL_INF);

#ifndef WALLFREE_VERSION
#define WALLFREE_VERSION "unbekannt"
#endif

/* Kennung im Abbild: die Weboberflaeche erkennt daran vor einem Update ein
 * Wallfree-Abbild und dessen Version (SOLL-BOOT-32) */
#define IMAGE_TAG "WALLFREE-IMAGE:"
static const char image_tag[] = IMAGE_TAG WALLFREE_VERSION;

static enum wf_reset reason;
static uint32_t flags;
static uint32_t update_result; /* BKP12: Modus << 8 | Erfolg, 0 = keins */

void sysinfo_init(void)
{
	uint32_t bkp;

	hw_reset_take(&flags, &bkp);
	reason = wf_reset_classify(flags, bkp);
	update_result = hw_update_result_take();
	LOG_INF("Wallfree %s, Resetgrund %s (Flags 0x%02x)", sysinfo_version(),
		wf_reset_name(reason), flags);
	if (update_result != 0) {
		LOG_INF("Update (Modus %u): %s", (update_result >> 8) & 0xFF,
			(update_result & 1) ? "erfolgreich" : "fehlgeschlagen");
	}
}

bool sysinfo_update_result(uint8_t *mode, bool *ok)
{
	*mode = (update_result >> 8) & 0xFF;
	*ok = (update_result & 1) != 0;
	return update_result != 0;
}

enum wf_reset sysinfo_reset_reason(void)
{
	return reason;
}

uint32_t sysinfo_reset_flags(void)
{
	return flags;
}

const char *sysinfo_version(void)
{
	return image_tag + sizeof(IMAGE_TAG) - 1;
}

void sysinfo_json(struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "firmware", true);
	wf_jb_str(b, sysinfo_version());
	wf_jb_key(b, "reset_reason", false);
	wf_jb_str(b, wf_reset_name(reason));
	wf_jb_key(b, "reset_flags", false);
	wf_jb_int(b, flags);
	wf_jb_key(b, "uptime_ms", false);
	wf_jb_int(b, k_uptime_get());
	wf_jb_key(b, "update_result", false);
	if (update_result == 0) {
		wf_jb_raw(b, "null");
	} else {
		wf_jb_raw(b, "{");
		wf_jb_key(b, "mode", true);
		wf_jb_int(b, (update_result >> 8) & 0xFF);
		wf_jb_key(b, "ok", false);
		wf_jb_raw(b, (update_result & 1) ? "true" : "false");
		wf_jb_raw(b, "}");
	}
	wf_jb_raw(b, "}");
}
