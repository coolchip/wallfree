/* SPDX-License-Identifier: Apache-2.0 */
/* Systeminformationen, siehe sysinfo.h. Nur gebaut. */
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "hw.h"
#include "netcfg.h"
#include "sysinfo.h"
#include "timesync.h"

LOG_MODULE_REGISTER(sysinfo, LOG_LEVEL_INF);

#ifndef WALLFREE_VERSION
#define WALLFREE_VERSION "unbekannt"
#endif
#ifndef WALLFREE_BUILD
#define WALLFREE_BUILD "unbekannt"
#endif

/* Kennung im Abbild "WALLFREE-IMAGE:<Version>+<Bauzeichen>": die Weboberflaeche erkennt daran
 * vor einem Update ein Wallfree-Abbild und dessen Version (SOLL-BOOT-32) */
#define IMAGE_TAG "WALLFREE-IMAGE:"
static const char image_tag[] = IMAGE_TAG WALLFREE_VERSION "+" WALLFREE_BUILD;

static enum wf_reset reason;
static enum wf_reset_detail detail; /* SOLL-RT-14: welche Ueberwachung bzw. Ausnahme */
static unsigned int detail_extra;   /* Ausnahmegrund bei WF_RSTD_FAULT */
static uint32_t fault_pc;
static uint32_t flags;
static uint32_t update_result; /* BKP12: Modus << 8 | Erfolg, 0 = keins */
static uint32_t boot_regs[HW_BOOT_REGS];
static int image_ok; /* SOLL-BOOT-35: 1 gueltig, 0 ungueltig, -1 nicht pruefbar */
static uint32_t image_crc;

void sysinfo_init(void)
{
	uint32_t bkp, det;

	hw_boot_regs(boot_regs); /* vor dem Loeschen der Flags und von BKP12 */
	hw_reset_take(&flags, &bkp, &det, &fault_pc);
	reason = wf_reset_classify(flags, bkp);
	detail = wf_reset_detail_decode(det, &detail_extra);
	update_result = hw_update_result_take();
	image_ok = hw_image_check(&image_crc);
	LOG_INF("Wallfree %s, Resetgrund %s%s%s (Flags 0x%02x)", sysinfo_version(),
		wf_reset_name(reason), detail != WF_RSTD_NONE ? " / " : "",
		wf_reset_detail_name(detail), flags);
	if (image_ok == 0) {
		LOG_ERR("Abbild ungueltig (CRC %08x passt nicht zum Anhang): kein Laden", image_crc);
	}
	if (update_result != 0) {
		LOG_INF("Update (Modus %u): %s", (update_result >> 8) & 0xFF,
			(update_result & 1) ? "erfolgreich" : "fehlgeschlagen");
	}
}

int sysinfo_image(uint32_t *crc)
{
	*crc = image_crc;
	return image_ok;
}

const uint32_t *sysinfo_boot_regs(void)
{
	return boot_regs;
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

enum wf_reset_detail sysinfo_reset_detail(unsigned int *extra, uint32_t *pc)
{
	*extra = detail_extra;
	*pc = fault_pc;
	return detail;
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
	wf_jb_str(b, WALLFREE_VERSION);
	wf_jb_key(b, "build", false);
	wf_jb_str(b, WALLFREE_BUILD);
	wf_jb_key(b, "reset_reason", false);
	wf_jb_str(b, wf_reset_name(reason));
	wf_jb_key(b, "reset_detail", false);
	if (detail == WF_RSTD_NONE) {
		wf_jb_raw(b, "null");
	} else {
		wf_jb_str(b, wf_reset_detail_name(detail));
	}
	wf_jb_key(b, "reset_flags", false);
	wf_jb_int(b, flags);
	wf_jb_key(b, "uptime_ms", false);
	wf_jb_int(b, k_uptime_get());
	int32_t temp;

	wf_jb_key(b, "mcu_temp_dc", false);
	if (hw_mcu_temp(&temp) == 0) {
		wf_jb_int(b, temp);
	} else {
		wf_jb_raw(b, "null");
	}
	wf_jb_key(b, "adc_vref_mv", false);
	if (hw_adc_vref(&temp) == 0) {
		wf_jb_int(b, temp);
	} else {
		wf_jb_raw(b, "null");
	}
	wf_jb_key(b, "time", false); /* SOLL-TIME-03 */
	timesync_json(b);
	char mac[18];

	netcfg_mac(mac);
	wf_jb_key(b, "mac", false);
	wf_jb_str(b, mac);
	wf_jb_key(b, "image", false);
	if (image_ok < 0) {
		wf_jb_raw(b, "null");
	} else {
		char crc[9];

		snprintf(crc, sizeof(crc), "%08x", image_crc);
		wf_jb_raw(b, "{");
		wf_jb_key(b, "crc", true);
		wf_jb_str(b, crc);
		wf_jb_key(b, "ok", false);
		wf_jb_raw(b, image_ok ? "true" : "false");
		wf_jb_raw(b, "}");
	}
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
