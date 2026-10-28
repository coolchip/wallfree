/* SPDX-License-Identifier: Apache-2.0 */
#ifndef WALLFREE_APP_EVSE_TASK_H_
#define WALLFREE_APP_EVSE_TASK_H_

#include <wallfree/api/events.h>
#include <wallfree/api/reset.h>
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
/**
 * Geordneter Neustart (SOLL-RT-22, SOLL-SH-11): Ladeende anfordern, auf
 * Schuetz aus warten (hoechstens 15 s), sicherer Zustand, Reset. Kehrt sofort
 * zurueck. @return 0 oder -EALREADY
 */
int evse_task_reboot_orderly(enum wf_reset code);

/**
 * Update-Uebergabe (SOLL-BOOT-30): wie der geordnete Neustart, am Ende aber an den
 * Bootloader mit @p ip / @p mask (netcfg_ipv4). Kehrt sofort zurueck.
 * @return 0 oder -EALREADY
 */
int evse_task_update_orderly(uint32_t ip, uint32_t mask);

/** IWDG gleich zu Beginn von main starten (SOLL-BOOT-03). @return 0 oder Fehler */
int evse_task_wdt_start(void);

/** IWDG bedienen, solange der Schutzthread noch nicht laeuft (Startablauf). */
void evse_task_wdt_feed(void);

/** Ueberwachte Schnittstellen-Threads (SOLL-RT-11) */
enum evse_iface {
	EVSE_IFACE_STORAGE = 0,
	EVSE_IFACE_WS,
	EVSE_IFACE_SYSWORKQ,
	EVSE_IFACE_HTTP, /* meldet Beginn und Ende jedes Aufrufs (evse_task_http_busy) */
	EVSE_IFACE_COUNT,
};

/** Lebenszeichen eines Schnittstellen-Threads (Limit 10 s). */
void evse_task_iface_beat(enum evse_iface id);

/** HTTP-Server betritt (true) bzw. verlaesst einen Aufruf der Anwendung (Limit 10 s je Aufruf). */
void evse_task_http_busy(bool busy);

/**
 * Naechstes Ereignis (SOLL-ERR-10) ab Nummer @p *seq, siehe wf_ev_ring_get.
 * Erkannt im Automatenthread, ausgegeben im WebSocket-Thread.
 */
bool evse_task_event_get(uint32_t *seq, struct wf_event *ev);

/** Ereignis aus einem anderen Thread einreihen (Zaehler, SOLL-MTR-14); seq vergibt der Puffer. */
void evse_task_event_push(struct wf_event ev);

void evse_task_maint_begin(void);
void evse_task_maint_end(void);

#endif
