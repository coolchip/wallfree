/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Energiezaehler ueber Modbus RTU, nur zur Anzeige (Soll-Spezifikation 12.5,
 * SOLL-MTR-*, F28). Reines C99 ohne Zephyr-Abhaengigkeit, auf dem PC testbar.
 *
 * Ein Modell ist eine Tabelle aus Registerbloecken und Werten (SOLL-MTR-05).
 * Der Bus ist hinter struct meter_bus gekapselt (SOLL-MTR-04): Register lesen,
 * warten, Uhrzeit. meter_cycle() fuehrt einen ganzen Abfragezyklus aus und
 * blockiert dabei (Pausen und Antwortfristen); Aufrufer ist ein eigener
 * Thread niedrigster Prioritaet.
 *
 * Werte sind ganze Zahlen in Tausendsteln der WARP-Einheit: mV, mA, mW, mHz,
 * Wh (= 0,001 kWh). Nicht threadsicher (Aufrufer sperrt).
 */
#ifndef WALLFREE_METER_METER_H_
#define WALLFREE_METER_METER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Messgroessen; Reihenfolge = Reihenfolge in meters/0/values */
enum meter_q {
	METER_U1 = 0, METER_U2, METER_U3, /**< Spannung L1..L3-N */
	METER_I1, METER_I2, METER_I3,     /**< Strom L1..L3 */
	METER_P1, METER_P2, METER_P3,     /**< Wirkleistung L1..L3, Bezug positiv */
	METER_P,                          /**< Wirkleistung gesamt */
	METER_F,                          /**< Frequenz */
	METER_E_IMP,                      /**< Energie Bezug */
	METER_E_EXP,                      /**< Energie Einspeisung */
	METER_Q_COUNT,
};

/** Parameter meter_type (10.1) */
enum meter_type {
	METER_NONE = 0,
	METER_ABB_B23,
	METER_SDM630,
	METER_SDM72_V2,
	METER_SDM72_V1,
	METER_TYPE_COUNT,
};

/** SOLL-MTR-14 */
enum meter_status {
	METER_OFF = 0,
	METER_CONNECTING,
	METER_OK,
	METER_ERROR,
};

/* Ergebnis von meter_bus.read; Werte > 0 sind Modbus-Ausnahmecodes */
#define METER_BUS_OK      0
#define METER_BUS_TIMEOUT (-1) /**< keine Antwort innerhalb der Frist */
#define METER_BUS_FRAME   (-2) /**< CRC, Laenge, Adresse oder Funktion falsch */

/* Zeiten (SOLL-MTR-10..14, Nutzer 2026-10-21) */
#define METER_PERIOD_MS        1000
#define METER_PERIOD_ERROR_MS  10000
#define METER_GAP_MS           50   /**< Ruhepause vor jeder Anfrage */
#define METER_RESPONSE_MS      500  /**< Antwortfrist, auch Zusatzpause nach Timeout */
#define METER_TRIES            3
#define METER_FAILS_ERROR      3    /**< Zyklen ohne gueltigen Satz bis METER_ERROR */
#define METER_STALE_MS         10000

struct meter_bus {
	/**
	 * Holding- (fc 3) oder Input-Register (fc 4) lesen, hoechstwertiges Byte
	 * zuerst bereits zu uint16_t zusammengesetzt.
	 * @return METER_BUS_OK, METER_BUS_TIMEOUT, METER_BUS_FRAME oder Ausnahmecode
	 */
	int (*read)(void *ctx, uint8_t unit, uint8_t fc, uint16_t reg, uint16_t n,
		    uint16_t *out);
	void (*sleep_ms)(void *ctx, uint32_t ms);
	int64_t (*now_ms)(void *ctx);
	void *ctx;
};

/** SOLL-MTR-15, seit dem Start */
struct meter_stats {
	uint32_t requests;
	uint32_t responses;
	uint32_t timeouts;
	uint32_t frame_errors;
	uint32_t exceptions;
	uint32_t implausible;
	uint32_t sets;
	uint8_t last_exception;
};

/* Ereignisse fuer SOLL-ERR-10, von meter_take_events() geliefert */
#define METER_EV_STATUS      0x01 /**< Zustand hat gewechselt */
#define METER_EV_ENERGY_DOWN 0x02 /**< Zaehlerstand gesunken (SOLL-MTR-13) */

struct meter_model;

struct meter {
	const struct meter_model *model; /**< NULL = kein Zaehler */
	uint8_t type;
	uint8_t unit;
	struct meter_bus bus;
	uint8_t status;       /**< enum meter_status */
	uint8_t fails;        /**< Zyklen in Folge ohne gueltigen Satz */
	bool after_timeout;   /**< vor der naechsten Anfrage Antwortfrist abwarten */
	bool have_set;
	int64_t t_set;        /**< Zeitpunkt des letzten gueltigen Satzes */
	int64_t v[METER_Q_COUNT];
	bool have[METER_Q_COUNT]; /**< Wert im letzten Satz geliefert (keine Ungueltig-Kennung) */
	struct meter_stats st;
	uint8_t events;
};

/** Momentaufnahme fuer die Schnittstellen */
struct meter_values {
	uint8_t type;
	uint8_t status;
	int64_t age_ms;               /**< -1 = noch kein gueltiger Satz */
	bool valid[METER_Q_COUNT];    /**< vorhanden und nicht veraltet */
	int64_t v[METER_Q_COUNT];
	struct meter_stats st;
};

/** @p type ausserhalb des Bereichs gilt als METER_NONE. */
void meter_init(struct meter *m, uint8_t type, uint8_t unit, const struct meter_bus *bus);

/**
 * Einen Abfragezyklus ausfuehren (SOLL-MTR-10..14).
 * @return Abstand bis zum Beginn des naechsten Zyklus in ms
 */
uint32_t meter_cycle(struct meter *m);

void meter_values(const struct meter *m, int64_t now, struct meter_values *out);

/** Ereignisse seit dem letzten Aufruf (METER_EV_*), danach geloescht. */
uint8_t meter_take_events(struct meter *m);

/** Liefert das Modell diese Messgroesse? */
bool meter_has(uint8_t type, enum meter_q q);

/** Kennung der WARP-Messwertliste (meter_value_id) */
uint16_t meter_warp_id(enum meter_q q);

const char *meter_status_name(uint8_t status);
const char *meter_type_name(uint8_t type);

/** @return Anzahl der Register, die das Modell je Zyklus liest (0 = kein Zaehler) */
uint16_t meter_regs_total(uint8_t type);

/**
 * Nur Simulation und Tests (meter_sim.c, nicht auf der Steuerung gebaut):
 * Antwort eines Zaehlers des Modells @p type auf eine Leseanfrage aus den
 * Werten @p v (Tausendstel) bilden; Groessen ohne Wert erhalten die
 * Ungueltig-Kennung bzw. NaN.
 * @return METER_BUS_OK oder Ausnahmecode 2 (Adresse/Anzahl passt zu keinem Block)
 */
int meter_sim_encode(uint8_t type, uint8_t fc, uint16_t reg, uint16_t n, const int64_t *v,
		     uint16_t *out);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_METER_METER_H_ */
