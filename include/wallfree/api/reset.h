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

/**
 * Genauerer Ausloeser eines Software-Resets (SOLL-RT-14): welche Ueberwachung angeschlagen hat
 * bzw. dass eine Prozessorausnahme vorlag. Liegt in einem zweiten Backup-Register.
 */
enum wf_reset_detail {
	WF_RSTD_NONE = 0,
	WF_RSTD_CHARGER,        /**< Automatenthread meldet sich nicht (SOLL-RT-10) */
	WF_RSTD_BLOCKS,         /**< keine Messbloecke (SOLL-RT-10) */
	WF_RSTD_IFACE_STORAGE,  /**< Speicher-Thread haengt (SOLL-RT-11) */
	WF_RSTD_IFACE_WS,       /**< WebSocket-Thread haengt */
	WF_RSTD_IFACE_SYSWORKQ, /**< System-Workqueue haengt */
	WF_RSTD_FAULT,          /**< Prozessorausnahme, Grund und PC dazu */
	WF_RSTD_COUNT,
};

/** Kennung "WD" in den oberen 16 Bit, darunter Detail (8 Bit) und Zusatz (8 Bit). */
#define WF_RSTD_BKP_MAGIC 0x57440000u
static inline uint32_t wf_reset_detail_bkp(enum wf_reset_detail d, unsigned int extra)
{
	return WF_RSTD_BKP_MAGIC | ((uint32_t)d << 8) | (extra & 0xFFu);
}

/** Detail aus dem Backup-Register, WF_RSTD_NONE bei fehlender Kennung. */
enum wf_reset_detail wf_reset_detail_decode(uint32_t bkp, unsigned int *extra);

/** Kurzname des Details (englisch, fuer API), "" fuer WF_RSTD_NONE */
const char *wf_reset_detail_name(enum wf_reset_detail d);

/** Grund bestimmen. @p bkp = Inhalt des Backup-Registers (0, wenn leer). */
enum wf_reset wf_reset_classify(uint32_t flags, uint32_t bkp);

/** Kurzname (englisch, fuer API) */
const char *wf_reset_name(enum wf_reset r);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_RESET_H_ */
