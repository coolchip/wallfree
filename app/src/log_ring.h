/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_LOG_RING_H_
#define WALLFREE_APP_LOG_RING_H_

struct shell;

/** Juengste Logmeldungen ausgeben (etwa 1,9 KB, ganze Zeilen; SOLL-SH-22). */
void log_ring_print(const struct shell *sh);

/** Ring leeren. */
void log_ring_clear(void);

#endif
