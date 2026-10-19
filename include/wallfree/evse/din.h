/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Digitaleingaenge LD/EN/ML/XR/IN mit konfigurierbaren Funktionen
 * (SOLL-IO-10..12, Abschnitt 9.1, SOLL-FN-01..03).
 */
#ifndef WALLFREE_EVSE_DIN_H_
#define WALLFREE_EVSE_DIN_H_

#include <wallfree/evse/common.h>

#ifdef __cplusplus
extern "C" {
#endif

enum evse_din_ch {
	EVSE_DIN_LD = 0,
	EVSE_DIN_EN,
	EVSE_DIN_ML,
	EVSE_DIN_XR,
	EVSE_DIN_IN,
	EVSE_DIN_COUNT,
};

/** Funktionscodes (Original Felder 520..524, `input_list`). */
enum evse_in_code {
	EVSE_IN_NONE = 0,
	EVSE_IN_EN = 1,
	EVSE_IN_XR = 2,
	EVSE_IN_LD = 3,
	EVSE_IN_ML = 4,
	EVSE_IN_CT_NO = 5,
	EVSE_IN_CT_NC = 6,
	EVSE_IN_PWM5 = 7,
	EVSE_IN_MAX_6A = 8,
	EVSE_IN_MAX_10A = 9,
	EVSE_IN_MAX_13A = 10,
	EVSE_IN_MAX_16A = 11,
	EVSE_IN_MAX_20A = 12,
	EVSE_IN_MAX_32A = 13,
	EVSE_IN_MAX_63A = 14,
	EVSE_IN_MAX_70A = 15,
	EVSE_IN_EN_PULSED = 16,
	EVSE_IN_ML_PULSED = 17,
	EVSE_IN_EN_PORTAL = 18,
	EVSE_IN_MAX_CURRENT = 19,
	EVSE_IN_PAUSE = 20,
	EVSE_IN_ERROR = 21,
	EVSE_IN_LD_PULSED = 22,
	EVSE_IN_CODE_MAX = 22,
};

#define EVSE_DIN_ON_MV   9000 /* > 9,0 V ein */
#define EVSE_DIN_OFF_MV  3000 /* < 3,0 V aus */
#define EVSE_DIN_AGREE   2    /* Bloecke fuer die Entprellung (F2) */
#define EVSE_DIN_TOGGLE_MS 200

/** Ereignis fuer den Verriegelungswunsch (ML). */
enum evse_ml_event {
	EVSE_ML_EV_NONE = 0,
	EVSE_ML_EV_SET0,
	EVSE_ML_EV_SET1,
	EVSE_ML_EV_TOGGLE,
};

struct evse_din_config {
	uint8_t code[EVSE_DIN_COUNT];
};

struct evse_din_channel {
	bool hyst;    /**< nach Hysterese, ohne Entprellung */
	bool stable;  /**< entprellt (SOLL-IO-12) */
	uint8_t agree;
};

struct evse_din {
	struct evse_din_channel ch[EVSE_DIN_COUNT];
	bool en_toggle;
	evse_ms_t en_toggle_last;
	evse_ms_t ml_toggle_last;

	/* Ergebnisse (ODER ueber alle Kanaele derselben Funktion, F26) */
	bool en;           /**< Freigabe ueber Eingang (Codes 1, 16, 18) */
	bool xr;           /**< Verfuegbarkeit (Code 2) */
	bool ld;           /**< LD permanent (Code 3) */
	bool ct_no;        /**< Code 5 */
	bool ct_nc;        /**< Code 6 */
	bool pwm5;         /**< Code 7 */
	int32_t limit_ma;  /**< kleinste aktive Grenze der Codes 8..15, 0 = keine */
	bool max_current;  /**< Code 19 */
	bool pause;        /**< Code 20 */
	bool error_input;  /**< Code 21 */
	enum evse_ml_event ml_event; /**< Ereignis dieses Aufrufs */
};

/** Gueltiger Funktionscode? (SOLL-FN-01) */
bool evse_in_code_valid(int code);

void evse_din_init(struct evse_din *din);

/** Einen Messblock auswerten. */
void evse_din_update(struct evse_din *din, const struct evse_din_config *cfg,
		     const int32_t mv[EVSE_DIN_COUNT], evse_ms_t now);

/** Strom in mA fuer einen Code 8..15, sonst 0. */
int32_t evse_in_code_limit_ma(int code);

/** LD-Quelle (SOLL-LOCK-03). */
enum evse_ld_mode {
	EVSE_LD_NONE = 0,
	EVSE_LD_PERMANENT,
	EVSE_LD_PULSED,
};

/**
 * Erster Kanal (LD, EN, ML, XR, IN) mit Code 22, sonst mit Code 3.
 * @param ch_out Kanal oder -1
 */
enum evse_ld_mode evse_din_ld_source(const struct evse_din_config *cfg, int *ch_out);

#ifdef __cplusplus
}
#endif

#endif /* WALLFREE_EVSE_DIN_H_ */
