/* SPDX-License-Identifier: Apache-2.0 */
/* Systeminformationen: Resetgrund (SOLL-RT-14), Firmwarestand, Laufzeit */
#ifndef WALLFREE_APP_SYSINFO_H_
#define WALLFREE_APP_SYSINFO_H_

#include <stdbool.h>
#include <stdint.h>
#include <wallfree/api/json.h>
#include <wallfree/api/reset.h>

/** Beim Start, vor allem anderen: Resetgrund lesen und Flags loeschen. */
void sysinfo_init(void);

enum wf_reset sysinfo_reset_reason(void);
uint32_t sysinfo_reset_flags(void);
const char *sysinfo_version(void);

/**
 * Ergebnis eines Updates vor diesem Start (SOLL-BOOT-33, BKP12 des Bootloaders).
 * @return false, wenn keins vorliegt; sonst Modus und Erfolg.
 */
bool sysinfo_update_result(uint8_t *mode, bool *ok);

/** SOLL-BOOT-35: @return 1 Abbild gueltig, 0 ungueltig, -1 nicht pruefbar; @p crc gerechnet */
int sysinfo_image(uint32_t *crc);

/** Diagnose: RCC_CSR und BKP10..16 beim Start (hw_boot_regs). */
const uint32_t *sysinfo_boot_regs(void);

/**
 * {"firmware": ..., "reset_reason": ..., "reset_flags": n, "uptime_ms": n,
 *  "mac": "aa:bb:..", "image": null | {"crc": "hex", "ok": bool}, "update_result": null | {"mode": n, "ok": bool}}
 */
void sysinfo_json(struct wf_jbuf *b);

#endif
