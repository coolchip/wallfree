/* SPDX-License-Identifier: Apache-2.0 */
/* Systeminformationen: Resetgrund (SOLL-RT-14), Firmwarestand, Laufzeit */
#ifndef WALLFREE_APP_SYSINFO_H_
#define WALLFREE_APP_SYSINFO_H_

#include <wallfree/api/json.h>
#include <wallfree/api/reset.h>

/** Beim Start, vor allem anderen: Resetgrund lesen und Flags loeschen. */
void sysinfo_init(void);

enum wf_reset sysinfo_reset_reason(void);
uint32_t sysinfo_reset_flags(void);
const char *sysinfo_version(void);

/** {"firmware": ..., "reset_reason": ..., "reset_flags": n, "uptime_ms": n} */
void sysinfo_json(struct wf_jbuf *b);

#endif
