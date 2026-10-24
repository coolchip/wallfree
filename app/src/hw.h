/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Hardwareschicht der Steuerung EV-CC-AC1-M3-CBC-RCM-ETH.
 * Pinbelegung und Skalierung: Firmwarebefunde des Originals (wallbe2,
 * docs/takt-adc-gpio.md), keine Schaltplanbestaetigung. Nur gebaut, auf
 * Hardware nicht ausgefuehrt und nicht freigegeben.
 */
#ifndef WALLFREE_APP_HW_H_
#define WALLFREE_APP_HW_H_

#include <zephyr/kernel.h>
#include <wallfree/api/reset.h>
#include <wallfree/evse/charger.h>

/** Ein Messblock: Mittel ueber 8 PWM-Perioden, Skalierung wie Original ("mV"). */
struct hw_block {
	int32_t cp_pos;
	int32_t cp_neg;
	int32_t pp;
	int32_t din[EVSE_DIN_COUNT];
	int32_t supply;
	int32_t temp_raw;
	uint32_t seq;
	int64_t at_ms;
};

/* ---- hw_stm32.c: TIM1, ADC1, DMA2, MCO1, sicherer Zustand ---- */

/** CP-PWM und Messung starten (SOLL-RT-01..03). */
int hw_analog_init(void);

/** Tastgrad in Promille (0 = -12 V, 1000 = +12 V). */
void hw_cp_set_duty(uint16_t permille);

/** Letzten Messblock kopieren. @return false, wenn noch keiner vorliegt. */
bool hw_get_block(struct hw_block *out);

/** Wird bei jedem neuen Messblock gegeben. */
extern struct k_sem hw_block_sem;

/**
 * Sicherer Zustand ueber direkte Registerzugriffe, auch aus Fault-Handlern
 * (SOLL-RT-20/21): Schuetz aus, CP -12 V, Aktor in Ruhe, kein RCM-Test.
 */
void hw_safe_state_regs(void);

/** Schuetz sofort aus (Registerzugriff, ISR-tauglich). */
void hw_contactor_off_regs(void);

/**
 * Systemreset mit Grund (SOLL-RT-14): sicherer Zustand, Code in RTC-Backup-
 * Register 19, Reset. Ohne Zephyr-REBOOT, damit 'kernel reboot' entfaellt.
 */
FUNC_NORETURN void hw_reset_with(enum wf_reset code);

/**
 * Fehlerort fuer die Diagnose ohne Log (z. B. Absturz vor main): Programmzaehler in
 * Backup-Register 17, 0xFA00 << 16 | Grund in Register 18; bleiben bis zum naechsten
 * Fehler oder Stromausfall stehen und sind per SWD lesbar.
 */
void hw_fault_note(unsigned int reason, uint32_t pc);

/**
 * Beim Start: Reset-Flags (RCC_CSR) und Code aus Backup-Register 19 lesen
 * und beide loeschen. @p flags als WF_RSTF_*.
 */
void hw_reset_take(uint32_t *flags, uint32_t *bkp);

/**
 * Beim Start: Fehlerbits in FLASH_SR lesen und loeschen. Der Zephyr-Treiber laesst
 * sonst den ersten Schreibzugriff mit -EIO scheitern, wenn beim Start ein Fehlerbit
 * stehen blieb (Testgeraet 2026-10-24, Herkunft offen). @return vorgefundene Fehlerbits
 */
uint32_t hw_flash_take_errors(void);

/** Fehlerbits in FLASH_SR, gelesen vor allen Treibern (Diagnose der Herkunft). */
uint32_t hw_flash_errors_early(void);

/**
 * Uebergabe an den Original-Bootloader (SOLL-BOOT-30/31): sicherer Zustand, BKP10 = 2
 * (Web-Update), BKP11 = 3 Versuche, BKP15 = 0, BKP13/14 = IP und Maske in lwIP-Byteordnung
 * (0 = Bootloader nimmt 192.168.0.8), Resetgrund "update" in BKP19, Systemreset.
 */
FUNC_NORETURN void hw_bootloader_handover(uint32_t ip, uint32_t mask);

/** Beim Start: BKP12 (Ergebnis eines Updates, SOLL-BOOT-33) lesen und loeschen. */
uint32_t hw_update_result_take(void);

/** IWDG starten (SOLL-BOOT-03). @return 0 oder Fehler */
int hw_wdt_start(uint32_t timeout_ms);

/** IWDG bedienen. */
void hw_wdt_feed(void);

/**
 * IWDG-Limit fuer Flash-Loeschen auf das Maximum setzen (true) bzw. den
 * vorherigen Wert wiederherstellen (false), SOLL-RT-13.
 */
void hw_iwdg_long(bool on);

/* ---- hw_gpio.c: digitale Ein-/Ausgaenge, Schutz-Interrupts ---- */

int hw_gpio_init(void);
void hw_gpio_read_switches(evse_dip_t *dip, uint8_t *rotary);
bool hw_gpio_reset_button(void);
/** Einzelne LED direkt setzen (nur vor dem Start der Ladelogik, z. B. Werksreset). */
void hw_gpio_led(enum evse_led led, bool on);
void hw_gpio_read_rcm(struct evse_rcm_levels *lv);
/** Netzwerk-LED PC2 [HYP] (SOLL-LED-06), unabhaengig von der Ladelogik. */
void hw_gpio_net_led(bool on);
int64_t hw_gpio_last_sense_edge(void);

/**
 * Ausgaenge setzen. Das Schuetz wird nur eingeschaltet, wenn kein
 * Schutz-Latch des ISR-Pfads ansteht (SOLL-PROT-01).
 */
void hw_gpio_apply(const struct evse_outputs *o);

/** Schutz-Latch: gesetzt durch RCM-Interrupt bzw. Schutzthread. */
bool hw_protect_latched(void);
void hw_protect_trip(void);
void hw_protect_clear(void);

#if defined(CONFIG_ARCH_POSIX)
/** PC-Simulation: HTTP-Port aus der Kommandozeile (--http-port). */
uint16_t hw_sim_http_port(void);
/** PC-Simulation: Netzverbindung laut Modell ("sim link"). */
bool hw_sim_link(void);
#endif

#endif /* WALLFREE_APP_HW_H_ */
