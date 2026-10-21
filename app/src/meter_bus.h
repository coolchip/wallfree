/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Busanbindung des Zaehlers (SOLL-MTR-04): Register lesen, sonst nichts.
 * Steuerung: Modbus-Client von Zephyr auf UART4 (meter_bus_modbus.c, F29);
 * Simulation: simulierter Zaehler im Hardwaremodell (hw_sim.c). Fuer eine
 * andere Modbus-Implementierung genuegt eine weitere Datei mit diesen zwei
 * Funktionen.
 */
#ifndef WALLFREE_APP_METER_BUS_H_
#define WALLFREE_APP_METER_BUS_H_

#include <wallfree/api/sysconf.h>

/** Schnittstelle mit Baudrate und Rahmenformat aus @p c einrichten. @return 0 oder Fehler */
int meter_bus_init(const struct wf_sysconf *c);

/** wie struct meter_bus.read (METER_BUS_OK, _TIMEOUT, _FRAME oder Ausnahmecode) */
int meter_bus_read(void *ctx, uint8_t unit, uint8_t fc, uint16_t reg, uint16_t n,
		   uint16_t *out);

#endif
