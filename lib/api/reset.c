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

const char *wf_reset_name(enum wf_reset r)
{
	static const char *const n[] = {
		"unknown", "power_on", "brownout", "reset_pin", "watchdog", "window_watchdog",
		"low_power", "software", "supervision_limit", "fault", "user_restart",
		"factory_reset", "interface_hang",
	};

	return (unsigned int)r < sizeof(n) / sizeof(n[0]) ? n[r] : "unknown";
}
