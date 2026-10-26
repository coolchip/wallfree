/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Uhrzeit per SNTP (SOLL-TIME-01..04): Hilfen ohne Netz, damit Ablauf und Grenzen auf dem PC
 * testbar sind. Die Ladelogik benutzt die Uhrzeit nie; ohne Zeitserver bleibt alles wie
 * vorher (Zeiten relativ zum Start). Reines C.
 */
#ifndef WALLFREE_API_NTP_H_
#define WALLFREE_API_NTP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WF_NTP_DEFAULT_SERVER "pool.ntp.org"
#define WF_NTP_PORT           123
#define WF_NTP_RESYNC_S       (6u * 3600u) /**< nach Erfolg */
#define WF_NTP_RETRY_MIN_S    60u          /**< nach Fehlschlag, verdoppelt ... */
#define WF_NTP_RETRY_MAX_S    3600u        /**< ... bis hoechstens */
/** Plausibel: 2025-01-01 .. 2100-01-01 (Unix-Sekunden) */
#define WF_NTP_MIN_EPOCH      1735689600ull
#define WF_NTP_MAX_EPOCH      4102444800ull

/**
 * "name", "a.b.c.d", jeweils optional ":port" zerlegen. Name: Buchstaben, Ziffern, '-', '.'.
 * @param n Laenge von @p s (ohne Nullzeichen)
 * @param host Puffer mit mindestens n + 1 Byte
 * @return 0 oder -1 (ungueltig, leer)
 */
int wf_ntp_parse_server(const char *s, size_t n, char *host, uint16_t *port);

/** Wartezeit bis zum naechsten Versuch in s: nach Erfolg (@p fails = 0) WF_NTP_RESYNC_S,
 *  sonst 60 s, 120 s, ... bis WF_NTP_RETRY_MAX_S */
uint32_t wf_ntp_next_s(unsigned int fails);

/**
 * Antwort in ms seit 1970 umrechnen und pruefen. @p seconds/@p fraction wie struct sntp_time
 * (Unix-Sekunden, Bruchteil in 2^-32 s). @return ms oder 0 (ausserhalb WF_NTP_*_EPOCH)
 */
int64_t wf_ntp_epoch_ms(uint64_t seconds, uint32_t fraction);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_API_NTP_H_ */
