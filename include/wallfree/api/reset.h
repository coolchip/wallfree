/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Grund des letzten Resets (SOLL-RT-14). Hardware-Flags (RCC_CSR, RM0033
 * Abschnitt RCC) und ein vor Software-Resets geschriebener Code im RTC-
 * Backup-Register werden zu einem Grund zusammengefasst. Reines C.
 */
#ifndef WALLFREE_API_RESET_H_
#define WALLFREE_API_RESET_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Hardware-Flags (unabhaengig von der Bitlage in RCC_CSR) */
#define WF_RSTF_LPWR (1u << 0)
#define WF_RSTF_WWDG (1u << 1)
#define WF_RSTF_IWDG (1u << 2)
#define WF_RSTF_SFT  (1u << 3)
#define WF_RSTF_POR  (1u << 4)
#define WF_RSTF_PIN  (1u << 5)
#define WF_RSTF_BOR  (1u << 6)

enum wf_reset {
	WF_RST_UNKNOWN = 0,
	WF_RST_POWER,        /**< Einschalten (POR/PDR) */
	WF_RST_BROWNOUT,     /**< Spannungseinbruch (BOR ohne POR) */
	WF_RST_PIN,          /**< Reset-Pin (z. B. Debugger) */
	WF_RST_IWDG,         /**< unabhaengiger Watchdog */
	WF_RST_WWDG,
	WF_RST_LOWPOWER,
	WF_RST_SOFT,         /**< Software-Reset ohne Code */
	/* Software-Resets mit Code im Backup-Register */
	WF_RST_LIMIT,        /**< Thread-/Messueberwachung (SOLL-RT-10) */
	WF_RST_FAULT,        /**< Prozessorausnahme (SOLL-RT-21) */
	WF_RST_USER,         /**< geordneter Neustart auf Anforderung (SOLL-RT-22) */
	WF_RST_FACTORY,      /**< nach Werkseinstellungen */
	WF_RST_NET_HANG,     /**< Netz-/Shell-Thread haengt (SOLL-RT-11) */
	WF_RST_UPDATE,       /**< Uebergabe an den Bootloader (SOLL-BOOT-30) */
	WF_RST_COUNT,
};

/** Kennung im Backup-Register: "WF" in den oberen 16 Bit, darunter der Code. */
#define WF_RST_BKP_MAGIC 0x57460000u

static inline uint32_t wf_reset_bkp_value(enum wf_reset code)
{
	return WF_RST_BKP_MAGIC | (uint32_t)code;
}

/** Grund bestimmen. @p bkp = Inhalt des Backup-Registers (0, wenn leer). */
enum wf_reset wf_reset_classify(uint32_t flags, uint32_t bkp);

/** Kurzname (englisch, fuer API) */
const char *wf_reset_name(enum wf_reset r);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_RESET_H_ */
