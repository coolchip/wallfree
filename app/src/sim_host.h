/* SPDX-License-Identifier: Apache-2.0 */
/* Host-Seite der Simulation (native_sim), siehe sim_host.c */
#ifndef WALLFREE_APP_SIM_HOST_H_
#define WALLFREE_APP_SIM_HOST_H_

/** Sofort neu starten wie ein Hardware-Reset (auch aus ISR), ohne Exit-Ablaeufe. */
void wf_sim_host_reset_now(void);

#endif
