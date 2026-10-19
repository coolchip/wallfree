/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_EVSE_TASK_H_
#define WALLFREE_APP_EVSE_TASK_H_

#include <wallfree/evse/charger.h>

/**
 * Ladelogik und Schutzpfad starten (SOLL-BOOT-07..10).
 * @param stored_lock gespeicherter Verriegelungszustand (Feld 465)
 */
int evse_task_start(const struct evse_config *cfg, enum evse_lock_state stored_lock);

/**
 * Zugriff auf die Ladelogik fuer Schnittstellen (SOLL-API-20/21): kurze,
 * begrenzte Sperre; nie blockierend fuer den Schutzpfad.
 */
struct evse *evse_acquire(void);
void evse_release(void);

/**
 * Wartung (Flash-Loeschen, SOLL-NVM-04/SOLL-RT-13): Thread- und
 * Messueberwachung aussetzen. Nur mit gehaltener evse_acquire()-Sperre im
 * sicheren Zustand verwenden; danach gelten die Limits wieder ab jetzt.
 */
void evse_task_maint_begin(void);
void evse_task_maint_end(void);

#endif
