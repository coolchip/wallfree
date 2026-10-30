/* SPDX-License-Identifier: Apache-2.0 */
/* Grund des letzten Resets, siehe include/wallfree/api/reset.h */
#include <wallfree/api/reset.h>

enum wf_reset wf_reset_classify(uint32_t flags, uint32_t bkp)
{
	/*
	 * Rangfolge: Watchdogs vor Software-Reset; jede interne Quelle erzeugt
	 * auch einen Puls am NRST-Pin (RM0033, Abschnitt Reset), deshalb PIN zuletzt.
	 * POR setzt zugleich BOR (RM0033 RCC_CSR Bit 25).
	 */
	if (flags & WF_RSTF_IWDG) {
		return WF_RST_IWDG;
	}
	if (flags & WF_RSTF_WWDG) {
		return WF_RST_WWDG;
	}
	if (flags & WF_RSTF_LPWR) {
		return WF_RST_LOWPOWER;
	}
	if (flags & WF_RSTF_SFT) {
		uint32_t code = bkp & 0xFFFFu;

		if ((bkp & 0xFFFF0000u) == WF_RST_BKP_MAGIC && code >= WF_RST_LIMIT &&
		    code < WF_RST_COUNT) {
			return (enum wf_reset)code;
		}
		return WF_RST_SOFT;
	}
	if (flags & WF_RSTF_POR) {
		return WF_RST_POWER;
	}
	if (flags & WF_RSTF_BOR) {
		return WF_RST_BROWNOUT;
	}
	if (flags & WF_RSTF_PIN) {
		return WF_RST_PIN;
	}
	return WF_RST_UNKNOWN;
}

enum wf_reset_detail wf_reset_detail_decode(uint32_t bkp, unsigned int *extra)
{
	unsigned int d = (bkp >> 8) & 0xFFu;

	*extra = 0;
	if ((bkp & 0xFFFF0000u) != WF_RSTD_BKP_MAGIC || d == WF_RSTD_NONE || d >= WF_RSTD_COUNT) {
		return WF_RSTD_NONE;
	}
	*extra = bkp & 0xFFu;
	return (enum wf_reset_detail)d;
}

const char *wf_reset_detail_name(enum wf_reset_detail d)
{
	static const char *const n[] = {
		"", "charger_thread", "measurement_blocks", "storage_thread", "websocket_thread",
		"system_workqueue", "fault", "http_server",
	};

	return (unsigned int)d < sizeof(n) / sizeof(n[0]) ? n[d] : "";
}

const char *wf_reset_name(enum wf_reset r)
{
	static const char *const n[] = {
		"unknown", "power_on", "brownout", "reset_pin", "watchdog", "window_watchdog",
		"low_power", "software", "supervision_limit", "fault", "user_restart",
		"factory_reset", "interface_hang", "update",
	};

	return (unsigned int)r < sizeof(n) / sizeof(n[0]) ? n[r] : "unknown";
}
