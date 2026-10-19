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

static enum wf_reset reason;
static uint32_t flags;

void sysinfo_init(void)
{
	uint32_t bkp;

	hw_reset_take(&flags, &bkp);
	reason = wf_reset_classify(flags, bkp);
	LOG_INF("Wallfree %s, Resetgrund %s (Flags 0x%02x)", WALLFREE_VERSION,
		wf_reset_name(reason), flags);
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
	return WALLFREE_VERSION;
}

void sysinfo_json(struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "firmware", true);
	wf_jb_str(b, WALLFREE_VERSION);
	wf_jb_key(b, "reset_reason", false);
	wf_jb_str(b, wf_reset_name(reason));
	wf_jb_key(b, "reset_flags", false);
	wf_jb_int(b, flags);
	wf_jb_key(b, "uptime_ms", false);
	wf_jb_int(b, k_uptime_get());
	wf_jb_raw(b, "}");
}
