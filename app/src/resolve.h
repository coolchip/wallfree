/* SPDX-License-Identifier: Apache-2.0 */
/* Namen zu IPv4 aufloesen (Zeitserver, MQTT-Broker), siehe resolve.c */
#ifndef WALLFREE_RESOLVE_H_
#define WALLFREE_RESOLVE_H_

#include <stdint.h>
#include <zephyr/net/net_ip.h>

/**
 * IPv4-Adresse oder Name per DNS (blockiert bis etwa 4 s, nur aus Threads). Ohne DNS (PC-
 * Simulation) nur Adressen. @return 0 und @p sa (Familie, Adresse, @p port) oder < 0
 */
int resolve4(const char *host, uint16_t port, struct net_sockaddr_in *sa);

#endif /* WALLFREE_RESOLVE_H_ */
