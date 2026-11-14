/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Ladeautomat (Abschnitt 5) mit Freigabe (9.2), Ausgangsfunktionen (9.3),
 * Fehlermodell (11.1) und LEDs (11.2).
 *
 * Aufrufmodell: evse_step() im Automatenzyklus (10 ms, SOLL-RT-05) und
 * zusaetzlich bei jedem neuen Messblock (new_block = true). Die Funktion
 * blockiert nie (SOLL-RT-04). Ergebnis sind die Sollwerte in
 * struct evse_outputs, die die Hardwareschicht ausgibt.
 *
 * Der ISR-Schutzpfad (SOLL-PROT-05) schaltet das Schuetz unabhaengig davon
 * direkt ab; evse_step() bewertet dieselben Signale zusaetzlich und haelt
 * die Ein-Sperre (SOLL-PROT-01).
 */
#ifndef WALLFREE_EVSE_CHARGER_H_
#define WALLFREE_EVSE_CHARGER_H_

#include <stddef.h>
#include <wallfree/evse/common.h>
#include <wallfree/evse/cp.h>
#include <wallfree/evse/ctm.h>
#include <wallfree/evse/current.h>
#include <wallfree/evse/din.h>
#include <wallfree/evse/errors.h>
#include <wallfree/evse/lock.h>
#include <wallfree/evse/pp.h>
#include <wallfree/evse/rcm.h>
#include <wallfree/evse/switches.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evse_state {
	EVSE_S0_INIT = 0,
	EVSE_S1_IDLE,
	EVSE_S2_LOCKING,
	EVSE_S3_WAIT,
	EVSE_S4_SELFTEST,
	EVSE_S5_PWM_ON,
	EVSE_S6_CHARGING,
	EVSE_S7_STOPPING,
	EVSE_S8_UNLOCKING,
	EVSE_S9_ERROR,
	EVSE_S10_UNAVAILABLE,
};

#define EVSE_STARTUP_MS        5000  /* E9 */
#define EVSE_STOP_TIMEOUT_MS   5000  /* E7b */
#define EVSE_PWM_SETTLE_MS     100   /* PWM aktiv vor Schuetz (Ist §6) */
#define EVSE_WAKE_NEG_MS       3000  /* E8 */
#define EVSE_WAKE_POS_MS       750
#define EVSE_DIODE_NEG_MS      3000  /* F7 */
#define EVSE_LD_LOST_MS        100
#define EVSE_RCM_RETEST_MS     30000 /* F13 */
#define EVSE_LOCK_ATTEMPTS     5
#define EVSE_UNLOCK_ATTEMPTS   4
#define EVSE_LOCK_E_FROM       3     /* ERR-LOCK erzwingt E ab dem 3. Fehlschlag */

/** Ausgangsklemmen */
enum evse_dout {
	EVSE_DOUT_ER = 0,
	EVSE_DOUT_LR,
	EVSE_DOUT_VR,
	EVSE_DOUT_CR,
	EVSE_DOUT_COUNT,
};

enum evse_led {
	EVSE_LED_POWER = 0,
	EVSE_LED_ERROR,
	EVSE_LED_CONNECT,
	EVSE_LED_READY,
	EVSE_LED_COUNT,
};

enum evse_led_mode {
	EVSE_LEDM_OFF = 0,
	EVSE_LEDM_ON,
	EVSE_LEDM_1HZ,
	EVSE_LEDM_2HZ,
	EVSE_LEDM_5HZ,
};

/** Ereignis der Fehlerhistorie (SOLL-ERR-01) */
struct evse_err_event {
	evse_ms_t t;
	evse_err_t set;      /**< neu aufgetretene Fehler */
	evse_err_t cleared;  /**< geloeschte Fehler */
	uint8_t state;       /**< Automatenzustand danach */
};
#define EVSE_ERR_HIST 32

/** Konfiguration (Abschnitt 10.1) und Schalterstellungen. */
struct evse_config {
	evse_dip_t dip;             /**< wirksame DIP-Stellung */
	uint8_t rotary;             /**< Drehschalter 0..15 */
	struct evse_din_config din; /**< in_function_* */
	uint8_t out_function[EVSE_DOUT_COUNT];
	uint16_t lock_pulse_ms;
	uint16_t unlock_pulse_ms;
	uint16_t lock_cycle_gap_ms;
	bool unlock_check;
	uint16_t contactor_min_off_ms;
	uint8_t contactor_monitor;
	bool reject_state_d;
	bool overcurrent_enable;
	uint32_t wakeup_delay_ms;   /**< 0 = keine Weckfolge */
	uint8_t wakeup_mode;        /**< 0 einmal je Steckvorgang, 1 auch nach Sperre */
	bool meter_available;       /**< Messquelle vorhanden (F1), derzeit false */
	int32_t external_current_default_ma; /**< Startwert Slot external (F12) */
	bool enable_register_default;        /**< Startwert Freigabe 400 (F19) */
};

/** Werkseinstellungen (E20, Abschnitt 10.1). */
void evse_config_defaults(struct evse_config *cfg);

/** Laufzeitwerte (Abschnitt 10.2). */
struct evse_runtime {
	int32_t external_ma;
	bool enable_register;
	bool available_register;
	bool pwm5_request;
	bool pause_request;
	bool lock_request;
	bool unlock_request;
	bool register_output[4];
	int32_t hardware_limit_ma; /**< abgesenkte Obergrenze, 0 = keine (SOLL-PAR-40) */
};

/** Eingaenge eines Aufrufs. */
struct evse_inputs {
	bool new_block;               /**< neuer Messblock liegt vor */
	int32_t cp_pos_mv;
	int32_t cp_neg_mv;
	int32_t pp_mv;
	int32_t din_mv[EVSE_DIN_COUNT];
	int32_t supply_mv;            /**< Versorgung, Skalierung wie Original (x 0,001) */
	struct evse_rcm_levels rcm;
	bool protect_tripped;         /**< Schutzpfad hat PE2 abgeschaltet (SOLL-PROT-14) */
	evse_ms_t last_sense_edge;    /**< PE0 */
	bool meter_valid;
	int32_t phase_v[3];
	int32_t phase_ma[3];
};

/** Sollwerte fuer die Hardware. */
struct evse_outputs {
	uint16_t cp_duty;            /**< Promille; 0 = -12 V, 1000 = +12 V */
	bool contactor;              /**< PE2 */
	bool lock_pulse;             /**< PA9 Low */
	bool unlock_pulse;           /**< PA10 Low */
	bool rcm_test;               /**< PE5 Low */
	bool dout[EVSE_DOUT_COUNT];  /**< ER, LR, VR, CR */
	bool led[EVSE_LED_COUNT];    /**< true = an (Pegel macht die Hardwareschicht) */
	enum evse_led_mode led_mode[EVSE_LED_COUNT];
	bool pe1;                    /**< D5-Ausgang, immer Low (F15) */
};

struct evse {
	struct evse_config cfg;
	struct evse_runtime rt;

	/* Module */
	struct evse_cp cp;
	struct evse_pp pp;
	struct evse_din din;
	struct evse_lock lock;
	struct evse_rcm rcm;
	struct evse_ctm ctm;
	struct evse_lock_config lock_cfg;
	struct evse_rotary rotary;
	struct evse_current_out cur;
	struct evse_current_in cur_in; /**< Eingaenge der letzten Strombewertung (Anzeige) */
	enum evse_ld_mode ld_mode;

	/* Automat */
	enum evse_state state;
	evse_ms_t state_since;
	evse_ms_t start_ms;
	enum evse_state stop_target;
	evse_ms_t stop_since;

	/* Steckvorgang */
	bool selftest_passed;
	bool wake_done;
	uint8_t wake_phase;          /**< 0 keine, 1 -12 V, 2 +12 V */
	evse_ms_t wake_since;
	evse_ms_t b_since;           /**< seit wann CP B in S5 */
	bool charged_this_plug;
	uint8_t lock_attempts;
	uint8_t unlock_attempts;
	bool lock_fail_unlocking;    /**< nach Fehlschlag Verriegeln: Entriegelungszyklus */
	bool unlock_fail_relocking;  /**< nach Fehlschlag Entriegeln: zurueck verriegeln */
	bool relock_mode;            /**< SOLL-LOCK-40 */
	evse_ms_t ld_lost_since;
	bool was_blocked;            /**< fuer Code 39 und wakeup_mode 1 */
	evse_ms_t charge_start;
	evse_ms_t charge_ms;

	/* Schuetz und CP */
	bool contactor;
	evse_ms_t contactor_off_since;
	evse_ms_t contactor_on_since;  /**< Ein-Befehl (SOLL-PROT-25) */
	evse_ms_t contactor_off_at_19; /**< Nachlauf Code 38 */
	uint16_t cp_duty;
	evse_ms_t pwm_since;
	evse_ms_t diode_since;

	/* Fehler */
	evse_err_t errors;
	evse_err_t errors_seen;      /**< seit Start aufgetreten (SOLL-ERR-01) */
	struct evse_err_event hist[EVSE_ERR_HIST]; /**< Ringpuffer, nur RAM */
	uint8_t hist_head;           /**< naechster Schreibplatz */
	uint8_t hist_count;
	evse_err_t hist_prev;
	bool shutdown;               /**< Neustart angefordert: geordnet beenden (SOLL-RT-22) */
	uint32_t warnings;
	evse_ms_t supply_low_since;
	evse_ms_t supply_good_since;
	evse_ms_t rcm_retest_last;
	bool init_selftest_started;
	bool init_unlock_started;
	bool last_lock_wish;         /**< fuer Ausgangscode 29 */
	bool relock_requested;
	evse_ms_t oc125_since;
	evse_ms_t oc110_since;
	bool safe;                   /**< sicherer Zustand eingenommen */
	struct evse_rcm_levels rcm_lv; /**< zuletzt gelesene RCM-Pegel */

	/* Status */
	enum evse_cp_state status;   /**< CP-Status mit Erzwingung (SOLL-CP-08) */
	struct evse_debounce status_deb;
	bool avail_missing;
	bool permission;
	bool release;
	bool digital_comm_unavailable; /**< SOLL-ERR-30 */

	struct evse_outputs out;
};

/**
 * Initialisieren (SOLL-BOOT-06..10). Liest DIP/Drehschalter aus @p cfg,
 * startet RCM-Selbsttest und Start-Entriegelung beim ersten evse_step().
 * @param stored_lock gespeicherter Verriegelungszustand (Original Feld 465)
 */
void evse_init(struct evse *e, const struct evse_config *cfg, enum evse_lock_state stored_lock,
	       evse_ms_t now);

/** Ein Zyklus. */
void evse_step(struct evse *e, const struct evse_inputs *in, evse_ms_t now);

/** Sicherer Zustand (SOLL-RT-20), z. B. vor einem Watchdog-Reset. */
void evse_safe_state(struct evse *e, evse_ms_t now);

/**
 * Ver-/Entriegelungszyklus von aussen (Original Feld 465, SOLL-PAR-40):
 * nur in S1 bei Schuetz aus. @return false, wenn nicht erlaubt oder busy.
 */
bool evse_lock_command(struct evse *e, bool lock, evse_ms_t now);

/**
 * RCM-Selbsttest auf Anforderung (SOLL-SH-07): nur in S1, CP A, Schuetz aus,
 * kein Test laeuft. @return true, wenn gestartet.
 */
bool evse_selftest_command(struct evse *e, evse_ms_t now);

/**
 * Neustart vorbereiten (SOLL-RT-22): wirkt wie fehlende Verfuegbarkeit,
 * laufendes Laden endet geordnet (S7, Frist 5 s), danach S10.
 */
void evse_request_shutdown(struct evse *e);

/** Schuetz aus (Soll und Ausgabe), Neustart jetzt zulaessig? */
static inline bool evse_contactor_is_off(const struct evse *e)
{
	return !e->contactor && !e->out.contactor;
}

/** Anzahl Eintraege der Fehlerhistorie. */
static inline size_t evse_err_hist_count(const struct evse *e)
{
	return e->hist_count;
}

/** Eintrag @p i der Fehlerhistorie, 0 = juengster. */
const struct evse_err_event *evse_err_hist_at(const struct evse *e, size_t i);

/** Schuetzfehler quittieren (F14, SOLL-ERR-03). @return true bei Erfolg. */
bool evse_ack_contactor_fault(struct evse *e);

/**
 * Abgeleitete Werte nach einer Konfigurationsaenderung neu bestimmen
 * (Verriegelung, LD-Quelle, Drehschalter, Konfigurationsfehler).
 */
void evse_apply_config(struct evse *e);

/**
 * Strom, den das Fahrzeug laut ausgegebenem Tastgrad ziehen darf (SOLL-CUR-21);
 * 0 ohne PWM (CP +12 V oder -12 V), Soll-Spezifikation 12.1.
 */
static inline int32_t evse_allowed_current(const struct evse *e)
{
	return (e->out.cp_duty == EVSE_DUTY_OFF || e->out.cp_duty == EVSE_DUTY_F) ? 0
										   : e->cur.allowed_ma;
}

/** Ausgangsfunktion @p code fuer den aktuellen Zustand (Tabelle 9.3). */
bool evse_dout_eval(const struct evse *e, uint8_t code, evse_ms_t now);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_CHARGER_H_ */
