/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Wallfree: Ersatzfirmware fuer EV-CC-AC1-M3-CBC-RCM-ETH.
 * Startablauf nach Soll-Spezifikation Abschnitt 13.2.
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <wallfree/evse/charger.h>
#include "evse_task.h"
#include "history_task.h"
#include "http_api.h"
#include "hw.h"
#include "meter_task.h"
#include "mqtt_task.h"
#include "netcfg.h"
#include "storage.h"
#include "sysinfo.h"
#include "timesync.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define BOOTLOADER_MS    5000  /* SOLL-BOOT-22: Loslassen ab hier -> Bootloader */
#define FACTORY_RESET_MS 10000 /* SOLL-BOOT-20 */
/* Schalter: PA11/PA12 haben nur den internen Pull-up; nach dem Einschalten braucht der
 * Pegel einige ms (Testgeraet las D8 sonst als an). Erst warten, dann stabil lesen. */
#define SWITCH_SETTLE_MS 20
#define SWITCH_STEP_MS   10
#define SWITCH_STABLE    3  /* gleiche Lesungen in Folge */
#define SWITCH_MAX_READS 30
#define BLINK_5HZ_MS     100  /* halbe Periode */
#define BLINK_1HZ_MS     500  /* halbe Periode */

enum button_action {
	BUTTON_NONE,
	BUTTON_BOOTLOADER,
	BUTTON_FACTORY_RESET,
};

/* SOLL-BOOT-20/22: Reset-Taster beim Start gehalten. Losgelassen vor 5 s -> normaler
 * Start, zwischen 5 und 10 s -> Bootloader, 10 s gehalten -> Werkseinstellungen.
 * Waehrend des Wartens wird der IWDG bedient (Schuetz ist aus); die Power-LED blinkt
 * erst mit 1 Hz, ab 5 s mit 5 Hz (SOLL-LED-01), sonst wirkt die Steuerung aus. */
static enum button_action check_reset_button(void)
{
	if (!hw_gpio_reset_button()) {
		return BUTTON_NONE;
	}
	int64_t t0 = k_uptime_get(), dt = 0;

	while (hw_gpio_reset_button()) {
		dt = k_uptime_get() - t0;
		evse_task_wdt_feed();
		hw_gpio_led(EVSE_LED_POWER, dt < BOOTLOADER_MS ? (dt / BLINK_1HZ_MS) % 2 == 0
							     : (dt / BLINK_5HZ_MS) % 2 == 0);
		if (dt >= FACTORY_RESET_MS) {
			return BUTTON_FACTORY_RESET;
		}
		k_msleep(10);
	}
	hw_gpio_led(EVSE_LED_POWER, false);
	return dt >= BOOTLOADER_MS ? BUTTON_BOOTLOADER : BUTTON_NONE;
}

/* SOLL-BOOT-06/SOLL-IO-30: einmal beim Start, aber erst wenn der Wert stabil ist */
static void read_switches_stable(evse_dip_t *dip, uint8_t *rotary)
{
	evse_dip_t d, last_d = 0;
	uint8_t r, last_r = 0;
	int same = 0;

	k_msleep(SWITCH_SETTLE_MS);
	for (int i = 0; i < SWITCH_MAX_READS && same < SWITCH_STABLE; i++) {
		evse_task_wdt_feed();
		hw_gpio_read_switches(&d, &r);
		same = (i > 0 && d == last_d && r == last_r) ? same + 1 : 1;
		last_d = d;
		last_r = r;
		k_msleep(SWITCH_STEP_MS);
	}
	if (same < SWITCH_STABLE) {
		LOG_WRN("Schalter nicht stabil, letzter Wert gilt");
	}
	*dip = last_d;
	*rotary = last_r;
}

/* SOLL-BOOT-20, SOLL-LED-01: nach dem Werksreset Power-LED an bis zum Loslassen */
static void light_until_released(void)
{
	hw_gpio_led(EVSE_LED_POWER, true);
	while (hw_gpio_reset_button()) {
		evse_task_wdt_feed();
		k_msleep(10);
	}
	hw_gpio_led(EVSE_LED_POWER, false);
}

int main(void)
{
	struct evse_config cfg;
	enum evse_lock_state lock = EVSE_LOCK_UNLOCKED;
	evse_dip_t dip;
	uint8_t rotary;

	sysinfo_init(); /* SOLL-RT-14: Resetgrund vor allem anderen sichern */
	if (hw_gpio_init() != 0) {
		/* ohne GPIO kein sicherer Betrieb: Ausgaenge bleiben im Startpegel */
		hw_safe_state_regs();
		return -1;
	}
	/* SOLL-BOOT-03: IWDG sofort; bis zum Schutzthread bedient main ihn */
	if (evse_task_wdt_start() != 0) {
		LOG_ERR("IWDG nicht verfuegbar");
		hw_safe_state_regs();
		return -1;
	}

	/* SOLL-BOOT-04/05: Parameterspeicher; Loeschen ist hier erlaubt (Ladelogik
	 * noch nicht gestartet, Ausgaenge im sicheren Startpegel) */
	(void)storage_init();
	evse_task_wdt_feed();
	switch (check_reset_button()) {
	case BUTTON_FACTORY_RESET:
		LOG_WRN("Werksreset: Parameterbereich wird geloescht");
		(void)storage_factory_reset_boot(); /* IWDG dabei verlaengert */
		light_until_released();
		break;
	case BUTTON_BOOTLOADER:
		/* SOLL-BOOT-22: Ladelogik nicht gestartet; ohne Adresse nimmt der Bootloader
		 * seine feste 192.168.0.8 */
		LOG_WRN("Reset-Taster: Uebergabe an den Bootloader (192.168.0.8)");
		for (int i = 0; i < 20; i++) { /* 200 ms fuer das Log, IWDG weiter bedienen */
			evse_task_wdt_feed();
			k_msleep(10);
		}
		hw_bootloader_handover(0, 0);
	default:
		break;
	}
	evse_config_defaults(&cfg);
	(void)storage_load(&cfg, &lock);
	evse_task_wdt_feed();

	/* SOLL-BOOT-06: DIP und Drehschalter nur beim Start lesen */
	read_switches_stable(&dip, &rotary);
	cfg.dip = dip;
	cfg.rotary = rotary;
	LOG_INF("DIP 0x%03x, Drehschalter %u", dip, rotary);

	hw_analog_init();                  /* SOLL-BOOT-07 */
	int r = evse_task_start(&cfg, lock); /* SOLL-BOOT-08..10 */
	uint8_t upd_mode;
	bool upd_ok;

	uint32_t img_crc;

	if (sysinfo_image(&img_crc) == 0) {
		/* SOLL-BOOT-35: ungueltiges Abbild -> Klasse P, kein Laden bis zum naechsten Start */
		evse_acquire()->errors |= EVSE_ERR_INTERNAL;
		evse_release();
	}
	unsigned int fault;
	uint32_t pc;
	enum wf_reset_detail det = sysinfo_reset_detail(&fault, &pc);

	/* SOLL-ERR-12: jeder Start steht mit Grund im Protokoll, das Neustarts uebersteht */
	evse_task_event_push((struct wf_event){.t = k_uptime_get(), .type = WF_EV_BOOT,
					       .a = sysinfo_reset_reason(), .b = det,
					       .set = pc, .cleared = fault});
	if (sysinfo_update_result(&upd_mode, &upd_ok)) {
		/* SOLL-BOOT-33 */
		evse_task_event_push((struct wf_event){
			.t = k_uptime_get(), .type = WF_EV_UPDATE, .a = upd_ok, .b = upd_mode});
	}

	storage_start();
	netcfg_init();          /* Hostname, DHCP bzw. feste Adresse */
	(void)http_api_start(); /* SOLL-BOOT-09: Netz verzoegert den Ladeablauf nicht */

	struct wf_sysconf sys;

	netcfg_get(&sys, NULL);
	meter_task_start(&sys); /* SOLL-MTR-01: ohne Zaehler nichts */
	mqtt_task_start();      /* SOLL-MQTT-01: ohne mqtt_enable keine Verbindung */
	history_task_start();   /* SOLL-API-33 */
	timesync_start();       /* SOLL-TIME-01: ohne Zeitserver nichts */
	return r;
}
