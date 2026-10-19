/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Wallfree: Ersatzfirmware fuer EV-CC-AC1-M3-CBC-RCM-ETH.
 * Startablauf nach Soll-Spezifikation Abschnitt 13.2. Nur gebaut, auf
 * Hardware nicht ausgefuehrt und nicht freigegeben.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <wallfree/evse/charger.h>
#include "evse_task.h"
#include "hw.h"
#include "storage.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define FACTORY_RESET_MS 5000 /* E22 */

/* SOLL-BOOT-20: Reset-Taster beim Start 5 s gedrueckt -> Werkseinstellungen */
static bool check_factory_reset(void)
{
	if (!hw_gpio_reset_button()) {
		return false;
	}
	int64_t t0 = k_uptime_get();

	while (hw_gpio_reset_button()) {
		if (k_uptime_get() - t0 >= FACTORY_RESET_MS) {
			return true;
		}
		k_msleep(10);
	}
	return false;
}

int main(void)
{
	struct evse_config cfg;
	enum evse_lock_state lock = EVSE_LOCK_UNLOCKED;
	evse_dip_t dip;
	uint8_t rotary;

	if (hw_gpio_init() != 0) {
		/* ohne GPIO kein sicherer Betrieb: Ausgaenge bleiben im Startpegel */
		hw_safe_state_regs();
		return -1;
	}

	/* SOLL-BOOT-04/05: Parameterspeicher; Loeschen ist hier erlaubt (Ladelogik
	 * noch nicht gestartet, Ausgaenge im sicheren Startpegel) */
	(void)storage_init();
	if (check_factory_reset()) {
		LOG_WRN("Werksreset: Parameterbereich wird geloescht");
		(void)storage_factory_reset_boot();
	}
	evse_config_defaults(&cfg);
	(void)storage_load(&cfg, &lock);

	/* SOLL-BOOT-06: DIP und Drehschalter nur beim Start lesen */
	hw_gpio_read_switches(&dip, &rotary);
	cfg.dip = dip;
	cfg.rotary = rotary;
	LOG_INF("DIP 0x%03x, Drehschalter %u", dip, rotary);

	hw_analog_init();                  /* SOLL-BOOT-07 */
	int r = evse_task_start(&cfg, lock); /* SOLL-BOOT-08..10 */

	storage_start();
	return r;
}
