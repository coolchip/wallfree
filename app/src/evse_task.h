/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_EVSE_TASK_H_
#define WALLFREE_APP_EVSE_TASK_H_

#include <wallfree/evse/charger.h>

/** Ladelogik und Schutzpfad starten (SOLL-BOOT-07..10). */
int evse_task_start(const struct evse_config *cfg);

/**
 * Zugriff auf die Ladelogik fuer Schnittstellen (SOLL-API-20/21): kurze,
 * begrenzte Sperre; nie blockierend fuer den Schutzpfad.
 */
struct evse *evse_acquire(void);
void evse_release(void);

#endif
