/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zaehlerbus ueber den Modbus-Client von Zephyr (F29), siehe meter_bus.h.
 * UART4 mit Treiberfreigabe PC12 laut Board-Devicetree (Knoten modbus0).
 *
 * Grenzen des Zephyr-Clients (docs/architektur.md, Speicherbudget): Ende
 * eines Telegramms nur ueber die Pause von 3,5 Zeichen; eine verspaetete
 * Antwort koennte der naechsten Anfrage zugeordnet werden. Die Zusatzpause
 * nach einem Timeout macht meter_cycle() (SOLL-MTR-11).
 */
#include <errno.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/modbus/modbus.h>
#include <wallfree/meter/meter.h>
#include "meter_bus.h"

LOG_MODULE_REGISTER(meter_bus, LOG_LEVEL_INF);

#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)

static int iface = -1;

int meter_bus_init(const struct wf_sysconf *c)
{
	static const enum uart_config_parity parity[] = {
		UART_CFG_PARITY_NONE, UART_CFG_PARITY_EVEN, UART_CFG_PARITY_ODD,
		UART_CFG_PARITY_NONE};
	/* 8N1 ist nicht normkonform: CONFIG_MODBUS_NONCOMPLIANT_SERIAL_MODE,
	 * dann gelten die Stoppbits hier fuer alle Formate */
	struct modbus_iface_param p = {
		.mode = MODBUS_MODE_RTU,
		.rx_timeout = METER_RESPONSE_MS * 1000,
		.serial = {
			.baud = c->meter_baud,
			.parity = parity[c->meter_framing & 3],
			.stop_bits = c->meter_framing == WF_FRAMING_8N2 ? UART_CFG_STOP_BITS_2
									: UART_CFG_STOP_BITS_1,
		},
	};

	iface = modbus_iface_get_by_name(DEVICE_DT_NAME(MODBUS_NODE));
	if (iface < 0) {
		LOG_ERR("Modbus-Schnittstelle fehlt");
		return iface;
	}
	int r = modbus_init_client(iface, p);

	if (r != 0) {
		LOG_ERR("Modbus-Client: %d", r);
		iface = -1;
	}
	return r;
}

int meter_bus_read(void *ctx, uint8_t unit, uint8_t fc, uint16_t reg, uint16_t n,
		   uint16_t *out)
{
	ARG_UNUSED(ctx);
	int r;

	if (iface < 0) {
		return METER_BUS_FRAME;
	}
	r = fc == 4 ? modbus_read_input_regs(iface, unit, reg, out, n)
		    : modbus_read_holding_regs(iface, unit, reg, out, n);
	if (r == 0 || r > 0) {
		return r; /* 0 oder Ausnahmecode */
	}
	return r == -ETIMEDOUT ? METER_BUS_TIMEOUT : METER_BUS_FRAME;
}
