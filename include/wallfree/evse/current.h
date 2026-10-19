/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Stromvorgabe: Slot-/Minimum-Modell und Tastgrad (Abschnitt 6,
 * SOLL-CUR-01..22). Alle Stroeme in mA.
 */
#ifndef WALLFREE_EVSE_CURRENT_H_
#define WALLFREE_EVSE_CURRENT_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVSE_CURRENT_MIN_MA 6000
#define EVSE_CURRENT_MAX_MA 80000
#define EVSE_DUTY_OFF       1000 /* +12 V DC */
#define EVSE_DUTY_F         0    /* -12 V DC */
#define EVSE_DUTY_PWM5      50

struct evse_current_in {
	int32_t hardware_ma;   /**< Slot hardware (Drehschalter bzw. abgesenkt) */
	bool hardware_pwm5;    /**< Drehschalter Position 0 */
	int32_t cable_ma;      /**< Slot cable, 0 = nicht aktiv */
	int32_t input_ma;      /**< Slot input (Codes 8..15), 0 = nicht aktiv */
	bool input_pwm5;       /**< Eingang Code 7 */
	int32_t external_ma;   /**< Slot external (Schnittstelle) */
	bool external_ignored; /**< Eingang Code 19 aktiv (F10) */
	bool pwm5_request;     /**< Schnittstelle (Original 401) */
	bool pause_input;      /**< Eingang Code 20 */
	bool pause_request;    /**< Schnittstelle (Original 468) */
};

struct evse_current_out {
	int32_t i_ma;          /**< wirksamer Sollstrom */
	bool blocked;          /**< Stromsperre (SOLL-CUR-11) */
	bool pwm5_mode;        /**< 5-%-Modus (SOLL-CUR-12) */
	bool implausible;      /**< SOLL-CUR-13 */
	uint16_t duty;         /**< Tastgrad in Promille */
	int32_t allowed_ma;    /**< Strom des Tastgrads (SOLL-CUR-21) */
};

void evse_current_eval(const struct evse_current_in *in, struct evse_current_out *out);

/** Groesster Tastgrad, dessen Strom nach HB Tab. 6-3 @p ma nicht uebersteigt. */
uint16_t evse_duty_from_ma(int32_t ma);

/** Strom in mA, den ein Fahrzeug aus dem Tastgrad ableitet; 0 = kein Laden. */
int32_t evse_ma_from_duty(uint16_t duty);

/** Zulaessiger Schreibwert fuer Strom-Slots (SOLL-CUR-14): 0 oder 6000..80000. */
bool evse_current_write_valid(int32_t ma);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_CURRENT_H_ */
