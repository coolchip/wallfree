/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Namen zu IPv4 aufloesen (SOLL-TIME-02, SOLL-MQTT-02). dns_get_addr_info statt getaddrinfo:
 * Letzteres legt die Ergebnisse mit k_calloc an, der System-Heap hat nur 512 B. Eine Abfrage
 * zur Zeit (Zeit- und MQTT-Thread teilen sich das Ergebnis), DNS-Server per DHCP bzw. bei
 * fester Adresse das Gateway (netcfg.c).
 */
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/net/dns_resolve.h>
#include "resolve.h"

#define DNS_TIMEOUT_MS 3000

#if defined(CONFIG_DNS_RESOLVER)
static K_MUTEX_DEFINE(dns_mtx);
static K_SEM_DEFINE(dns_done, 0, 1);
static struct net_sockaddr_in dns_addr;
static bool dns_ok;

static void dns_cb(enum dns_resolve_status status, struct dns_addrinfo *info, void *user_data)
{
	ARG_UNUSED(user_data);
	if (status == DNS_EAI_INPROGRESS && info != NULL && info->ai_family == NET_AF_INET) {
		memcpy(&dns_addr, &info->ai_addr, sizeof(dns_addr));
		dns_ok = true;
		return;
	}
	if (status != DNS_EAI_INPROGRESS) {
		k_sem_give(&dns_done); /* fertig (DNS_EAI_ALLDONE) oder Fehler */
	}
}
#endif

int resolve4(const char *host, uint16_t port, struct net_sockaddr_in *sa)
{
	memset(sa, 0, sizeof(*sa));
	sa->sin_family = NET_AF_INET;
	sa->sin_port = net_htons(port);
	if (net_addr_pton(NET_AF_INET, host, &sa->sin_addr) == 0) {
		return 0;
	}
#if defined(CONFIG_DNS_RESOLVER)
	int r = 0;

	k_mutex_lock(&dns_mtx, K_FOREVER);
	dns_ok = false;
	k_sem_reset(&dns_done);
	if (dns_get_addr_info(host, DNS_QUERY_TYPE_A, NULL, dns_cb, NULL, DNS_TIMEOUT_MS) < 0) {
		r = -EIO;
	} else {
		(void)k_sem_take(&dns_done, K_MSEC(DNS_TIMEOUT_MS + 1000));
		if (dns_ok) {
			sa->sin_addr = dns_addr.sin_addr;
		} else {
			r = -ENOENT;
		}
	}
	k_mutex_unlock(&dns_mtx);
	return r;
#else
	return -ENOTSUP; /* PC-Simulation: kein DNS */
#endif
}
