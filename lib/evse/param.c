/* SPDX-License-Identifier: Apache-2.0 */
/* Parametertabelle, Abschnitt 10 der Soll-Spezifikation */
#include <stddef.h>
#include <string.h>
#include <wallfree/evse/param.h>

#define CFG 0
#define RT  1
#define FN  2

#define CF(field) .base = CFG, .off = offsetof(struct evse_config, field), \
		  .size = sizeof(((struct evse_config *)0)->field)
#define RF(field) .base = RT, .off = offsetof(struct evse_runtime, field), \
		  .size = sizeof(((struct evse_runtime *)0)->field)

/* ------------------------------------------------------------ Pruefungen */

static int chk_in_code(const struct evse_config *cfg, int32_t v)
{
	(void)cfg;
	return evse_in_code_valid(v) ? EVSE_PARAM_OK : EVSE_PARAM_E_RANGE;
}

static int chk_current(const struct evse_config *cfg, int32_t v)
{
	(void)cfg;
	return evse_current_write_valid(v) ? EVSE_PARAM_OK : EVSE_PARAM_E_RANGE;
}

static int chk_wakeup(const struct evse_config *cfg, int32_t v)
{
	(void)cfg;
	return (v == 0 || (v >= 5000 && v <= 300000)) ? EVSE_PARAM_OK : EVSE_PARAM_E_RANGE;
}

static int chk_ctm(const struct evse_config *cfg, int32_t v)
{
	if (v == EVSE_CTM_METER && !cfg->meter_available) {
		return EVSE_PARAM_E_UNAVAILABLE; /* SOLL-PROT-22 */
	}
	return EVSE_PARAM_OK;
}

static int chk_overcurrent(const struct evse_config *cfg, int32_t v)
{
	if (v && !cfg->meter_available) {
		return EVSE_PARAM_E_UNAVAILABLE; /* SOLL-PROT-30 */
	}
	return EVSE_PARAM_OK;
}

/* ------------------------------------------------- Sonderfaelle (F3/E14) */

static int32_t get_hw_limit(const struct evse *e)
{
	return e->rt.hardware_limit_ma;
}

static int set_hw_limit(struct evse *e, int32_t v, evse_ms_t now)
{
	(void)now;
	/* SOLL-PAR-40: nur absenken; 0 hebt die Absenkung auf */
	if (v != 0 && (v < EVSE_CURRENT_MIN_MA || v > e->rotary.limit_ma)) {
		return EVSE_PARAM_E_RANGE;
	}
	e->rt.hardware_limit_ma = v;
	return EVSE_PARAM_OK;
}

static int32_t get_dip(const struct evse *e)
{
	return e->cfg.dip;
}

static int set_dip(struct evse *e, int32_t v, evse_ms_t now)
{
	(void)now;
	if (e->state != EVSE_S1_IDLE) {
		return EVSE_PARAM_E_STATE; /* SOLL-PAR-40: nur ohne Fahrzeug */
	}
	e->cfg.dip = (evse_dip_t)v;
	evse_apply_config(e);
	return EVSE_PARAM_OK;
}

static int32_t get_lock_state(const struct evse *e)
{
	return e->lock.state;
}

static int set_lock_state(struct evse *e, int32_t v, evse_ms_t now)
{
	if (v != 0 && v != 1) {
		return EVSE_PARAM_E_RANGE;
	}
	/* SOLL-PAR-40: echter Zyklus statt Merker */
	return evse_lock_command(e, v == 1, now) ? EVSE_PARAM_OK : EVSE_PARAM_E_STATE;
}

/* --------------------------------------------------------- Statuswerte */

#define GETTER(fn, expr)                                                                      \
	static int32_t fn(const struct evse *e)                                                \
	{                                                                                      \
		return (int32_t)(expr);                                                        \
	}

GETTER(st_state, e->state)
GETTER(st_cp_status, e->status)
GETTER(st_cp_raw, evse_cp_raw(&e->cp))
GETTER(st_cp_pos, e->cp.last_pos_mv)
GETTER(st_cp_neg, e->cp.last_neg_mv)
GETTER(st_cable, e->pp.cable_a)
GETTER(st_current, e->cur.i_ma)
GETTER(st_allowed, e->cur.allowed_ma)
GETTER(st_duty, e->out.cp_duty)
GETTER(st_blocked, e->cur.blocked)
GETTER(st_pwm5, e->cur.pwm5_mode)
GETTER(st_digital, e->digital_comm_unavailable)
GETTER(st_errors, e->errors)
GETTER(st_errors_seen, e->errors_seen)
GETTER(st_warnings, e->warnings)
GETTER(st_contactor, e->out.contactor)
GETTER(st_charge_ms, e->charge_ms)
GETTER(st_permission, e->permission)
GETTER(st_release, e->release)
GETTER(st_avail_missing, e->avail_missing)
GETTER(st_rotary, e->rotary.position)
GETTER(st_rcm_test, e->rcm.test)
GETTER(st_lock_attempts, e->lock_attempts)
GETTER(st_unlock_attempts, e->unlock_attempts)

static int32_t st_din_raw(const struct evse *e)
{
	int32_t m = 0;

	for (int i = 0; i < EVSE_DIN_COUNT; i++) {
		m |= e->din.ch[i].stable ? (1 << i) : 0;
	}
	return m;
}

static int32_t st_dout(const struct evse *e)
{
	int32_t m = 0;

	for (int i = 0; i < EVSE_DOUT_COUNT; i++) {
		m |= e->out.dout[i] ? (1 << i) : 0;
	}
	return m;
}

/* ------------------------------------------------------------- Tabelle */

#define STATUS(n, i, u, fn, h) {.name = n, .id = i, .unit = u, .kind = EVSE_PK_STATUS, \
				.base = FN, .get = fn, .help = h}

static const struct evse_param params[] = {
	/* 10.1 Konfiguration (persistent) */
	{.name = "out_function_er", .id = 327, .orig_field = 327, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 42, .def = 15, CF(out_function[EVSE_DOUT_ER]), .help = "Funktion Ausgang ER"},
	{.name = "out_function_lr", .id = 328, .orig_field = 328, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 42, .def = 29, CF(out_function[EVSE_DOUT_LR]), .help = "Funktion Ausgang LR"},
	{.name = "out_function_vr", .id = 329, .orig_field = 329, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 42, .def = 16, CF(out_function[EVSE_DOUT_VR]), .help = "Funktion Ausgang VR"},
	{.name = "out_function_cr", .id = 330, .orig_field = 330, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 42, .def = 17, CF(out_function[EVSE_DOUT_CR]), .help = "Funktion Ausgang CR"},
	{.name = "in_function_ld", .id = 520, .orig_field = 520, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 22, .def = 3, CF(din.code[EVSE_DIN_LD]), .check = chk_in_code,
	 .help = "Funktion Eingang LD"},
	{.name = "in_function_en", .id = 521, .orig_field = 521, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 22, .def = 1, CF(din.code[EVSE_DIN_EN]), .check = chk_in_code,
	 .help = "Funktion Eingang EN"},
	{.name = "in_function_ml", .id = 522, .orig_field = 522, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 22, .def = 4, CF(din.code[EVSE_DIN_ML]), .check = chk_in_code,
	 .help = "Funktion Eingang ML"},
	{.name = "in_function_xr", .id = 523, .orig_field = 523, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 22, .def = 2, CF(din.code[EVSE_DIN_XR]), .check = chk_in_code,
	 .help = "Funktion Eingang XR"},
	{.name = "in_function_in", .id = 524, .orig_field = 524, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 22, .def = 11, CF(din.code[EVSE_DIN_IN]), .check = chk_in_code,
	 .help = "Funktion Eingang IN"},
	{.name = "lock_pulse", .id = 525, .orig_field = 525, .unit = "ms", .kind = EVSE_PK_CONFIG,
	 .min = 100, .max = 3000, .def = 500, CF(lock_pulse_ms), .help = "Impuls Verriegeln"},
	{.name = "unlock_pulse", .id = 526, .orig_field = 526, .unit = "ms", .kind = EVSE_PK_CONFIG,
	 .min = 100, .max = 3000, .def = 500, CF(unlock_pulse_ms), .help = "Impuls Entriegeln"},
	{.name = "lock_cycle_gap", .id = 527, .orig_field = 527, .unit = "ms", .kind = EVSE_PK_CONFIG,
	 .min = 500, .max = 10000, .def = 2000, CF(lock_cycle_gap_ms),
	 .help = "Mindestabstand der Verriegelungszyklen"},
	{.name = "unlock_check", .id = 1001, .unit = "", .kind = EVSE_PK_CONFIG, .min = 0, .max = 1,
	 .def = 0, CF(unlock_check), .help = "LD gepulst auch beim Entriegeln pruefen (F27)"},
	{.name = "contactor_min_off", .id = 519, .orig_field = 519, .unit = "ms",
	 .kind = EVSE_PK_CONFIG, .min = 0, .max = 2000, .def = 500, CF(contactor_min_off_ms),
	 .help = "Mindestpause vor erneutem Schuetz-Ein"},
	{.name = "contactor_monitor", .id = 390, .orig_field = 390, .unit = "",
	 .kind = EVSE_PK_CONFIG, .min = 0, .max = 4, .def = 1, CF(contactor_monitor),
	 .check = chk_ctm, .help = "Schuetzueberwachung 0 aus, 1 Sense, 2 NO, 3 NC, 4 Zaehler"},
	{.name = "reject_state_d", .id = 412, .orig_field = 412, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 1, .def = 1, CF(reject_state_d), .help = "Zustand D abweisen"},
	{.name = "overcurrent_enable", .id = 409, .orig_field = 409, .unit = "",
	 .kind = EVSE_PK_CONFIG, .min = 0, .max = 1, .def = 0, CF(overcurrent_enable),
	 .check = chk_overcurrent, .help = "Ueberstromabschaltung (nur mit Messquelle)"},
	{.name = "wakeup_delay", .id = 1002, .unit = "ms", .kind = EVSE_PK_CONFIG, .min = 0,
	 .max = 300000, .def = 30000, CF(wakeup_delay_ms), .check = chk_wakeup,
	 .help = "Wartezeit in B bis zur Weckfolge, 0 = aus"},
	{.name = "wakeup_mode", .id = 1003, .unit = "", .kind = EVSE_PK_CONFIG, .min = 0, .max = 1,
	 .def = 0, CF(wakeup_mode), .help = "0 einmal je Steckvorgang, 1 auch nach Stromsperre"},
	{.name = "external_current_default", .id = 1004, .unit = "mA", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 80000, .def = 80000, CF(external_current_default_ma),
	 .check = chk_current, .is_signed = true, .help = "Startwert external_current"},
	{.name = "enable_register_default", .id = 1005, .unit = "", .kind = EVSE_PK_CONFIG,
	 .min = 0, .max = 1, .def = 0, CF(enable_register_default),
	 .help = "Startwert enable_register"},

	/* 10.2 Laufzeitwerte */
	{.name = "external_current", .id = 528, .orig_field = 528, .unit = "mA",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 80000, .def = 80000, RF(external_ma),
	 .check = chk_current, .is_signed = true, .help = "Sollstrom der Schnittstelle (evcc)"},
	{.name = "enable_register", .id = 400, .orig_field = 400, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, .def = 0, RF(enable_register),
	 .help = "Freigabe ueber Schnittstelle (D10)"},
	{.name = "available_register", .id = 402, .orig_field = 402, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, .def = 1, RF(available_register),
	 .help = "Verfuegbarkeit ueber Schnittstelle (D10)"},
	{.name = "pwm5_request", .id = 401, .orig_field = 401, .unit = "", .kind = EVSE_PK_RUNTIME,
	 .min = 0, .max = 1, .def = 0, RF(pwm5_request), .help = "5-%-Modus"},
	{.name = "pause_request", .id = 468, .orig_field = 468, .unit = "", .kind = EVSE_PK_RUNTIME,
	 .min = 0, .max = 1, .def = 0, RF(pause_request), .help = "Laden unterbrechen"},
	{.name = "lock_request", .id = 403, .orig_field = 403, .unit = "", .kind = EVSE_PK_RUNTIME,
	 .min = 0, .max = 1, .def = 0, RF(lock_request), .help = "Verriegelungswunsch (D9)"},
	{.name = "unlock_request", .id = 440, .orig_field = 440, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, .def = 0, RF(unlock_request),
	 .help = "Entriegeln-Anforderung"},
	{.name = "register_output_1", .id = 405, .orig_field = 405, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, RF(register_output[0]),
	 .help = "Ausgangscode 30"},
	{.name = "register_output_2", .id = 406, .orig_field = 406, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, RF(register_output[1]),
	 .help = "Ausgangscode 31"},
	{.name = "register_output_3", .id = 407, .orig_field = 407, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, RF(register_output[2]),
	 .help = "Ausgangscode 32"},
	{.name = "register_output_4", .id = 408, .orig_field = 408, .unit = "",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 1, RF(register_output[3]),
	 .help = "Ausgangscode 33"},
	{.name = "hardware_limit_override", .id = 801, .orig_field = 801, .unit = "mA",
	 .kind = EVSE_PK_RUNTIME, .min = 0, .max = 80000, .def = 0, .base = FN,
	 .get = get_hw_limit, .set = set_hw_limit, .help = "Obergrenze absenken, 0 = Drehschalter"},
	{.name = "dip_override", .id = 802, .orig_field = 802, .unit = "", .kind = EVSE_PK_RUNTIME,
	 .min = 0, .max = EVSE_DIP_MASK, .base = FN, .get = get_dip, .set = set_dip,
	 .help = "DIP D1..D10 (Bit 0..9), nur ohne Fahrzeug"},
	{.name = "lock_state", .id = 465, .orig_field = 465, .unit = "", .kind = EVSE_PK_RUNTIME,
	 .min = 0, .max = 5, .base = FN, .get = get_lock_state, .set = set_lock_state,
	 .help = "Verriegelung (WARP lock_state); Schreiben 0/1 loest Zyklus aus"},

	/* 10.3 Status */
	STATUS("state", 2000, "", st_state, "Automatenzustand S0..S10"),
	STATUS("cp_status", 2001, "", st_cp_status, "CP-Status A..F (0..5) mit Erzwingung"),
	STATUS("cp_raw", 2002, "", st_cp_raw, "CP-Rohzustand A..E (0..4)"),
	STATUS("cp_pos", 2003, "mV", st_cp_pos, "CP positives Plateau"),
	STATUS("cp_neg", 2004, "mV", st_cp_neg, "CP negatives Plateau (Betrag)"),
	STATUS("cable", 2005, "A", st_cable, "Kabelwert PP"),
	STATUS("current", 2006, "mA", st_current, "wirksamer Sollstrom"),
	STATUS("allowed_charging_current", 2007, "mA", st_allowed, "Strom des Tastgrads"),
	STATUS("cp_duty", 2008, "permille", st_duty, "CP-Tastgrad"),
	STATUS("current_blocked", 2009, "", st_blocked, "Stromsperre"),
	STATUS("pwm5_mode", 2010, "", st_pwm5, "5-%-Modus aktiv"),
	STATUS("digital_comm_unavailable", 2011, "", st_digital, "5 % und Fahrzeug C/D"),
	STATUS("errors", 2012, "", st_errors, "aktive Fehler (Bitmaske)"),
	STATUS("errors_seen", 2013, "", st_errors_seen, "seit Start aufgetretene Fehler"),
	STATUS("warnings", 2014, "", st_warnings, "Warnungen"),
	STATUS("contactor", 2015, "", st_contactor, "Schuetz Soll"),
	STATUS("charge_time", 2016, "ms", st_charge_ms, "Ladedauer des Steckvorgangs"),
	STATUS("permission", 2017, "", st_permission, "Ladeerlaubnis"),
	STATUS("release", 2018, "", st_release, "Freigabe"),
	STATUS("availability_missing", 2019, "", st_avail_missing, "Verfuegbarkeit fehlt"),
	STATUS("rotary", 2020, "", st_rotary, "Drehschalterstellung"),
	STATUS("rcm_selftest", 2021, "", st_rcm_test, "RCM-Selbsttest 0..4"),
	STATUS("inputs", 2022, "", st_din_raw, "Eingaenge LD..IN (Bit 0..4), entprellt"),
	STATUS("outputs", 2023, "", st_dout, "Ausgaenge ER..CR (Bit 0..3)"),
	STATUS("lock_attempts", 2024, "", st_lock_attempts, "Fehlversuche Verriegeln"),
	STATUS("unlock_attempts", 2025, "", st_unlock_attempts, "Fehlversuche Entriegeln"),
};

size_t evse_param_count(void)
{
	return sizeof(params) / sizeof(params[0]);
}

const struct evse_param *evse_param_at(size_t i)
{
	return i < evse_param_count() ? &params[i] : NULL;
}

const struct evse_param *evse_param_find(const char *name)
{
	for (size_t i = 0; i < evse_param_count(); i++) {
		if (strcmp(params[i].name, name) == 0) {
			return &params[i];
		}
	}
	return NULL;
}

const struct evse_param *evse_param_by_id(uint16_t id)
{
	for (size_t i = 0; i < evse_param_count(); i++) {
		if (params[i].id == id) {
			return &params[i];
		}
	}
	return NULL;
}

static void *field_ptr(const struct evse_param *p, struct evse_config *cfg, struct evse_runtime *rt)
{
	uint8_t *b = p->base == CFG ? (uint8_t *)cfg : (uint8_t *)rt;

	return b + p->off;
}

static int32_t load(const struct evse_param *p, const void *ptr)
{
	switch (p->size) {
	case 1:
		return *(const uint8_t *)ptr;
	case 2:
		return *(const uint16_t *)ptr;
	default:
		return *(const int32_t *)ptr;
	}
}

static void store(const struct evse_param *p, void *ptr, int32_t v)
{
	switch (p->size) {
	case 1:
		*(uint8_t *)ptr = (uint8_t)v;
		break;
	case 2:
		*(uint16_t *)ptr = (uint16_t)v;
		break;
	default:
		*(int32_t *)ptr = v;
		break;
	}
}

int32_t evse_param_get(const struct evse *e, const struct evse_param *p)
{
	if (p->base == FN) {
		return p->get(e);
	}
	return load(p, field_ptr(p, (struct evse_config *)&e->cfg, (struct evse_runtime *)&e->rt));
}

static int check_value(const struct evse_config *cfg, const struct evse_param *p, int32_t v)
{
	if (v < p->min || v > p->max) {
		return EVSE_PARAM_E_RANGE;
	}
	return p->check ? p->check(cfg, v) : EVSE_PARAM_OK;
}

int evse_param_set(struct evse *e, const struct evse_param *p, int32_t v, evse_ms_t now)
{
	if (p == NULL) {
		return EVSE_PARAM_E_UNKNOWN;
	}
	if (p->kind == EVSE_PK_STATUS) {
		return EVSE_PARAM_E_READONLY;
	}
	int err = check_value(&e->cfg, p, v);

	if (err != EVSE_PARAM_OK) {
		return err;
	}
	if (p->base == FN) {
		return p->set(e, v, now);
	}
	store(p, field_ptr(p, &e->cfg, &e->rt), v);
	if (p->kind == EVSE_PK_CONFIG) {
		evse_apply_config(e); /* SOLL-PAR-03: sofort wirksam */
	}
	return EVSE_PARAM_OK;
}

int evse_param_set_by_name(struct evse *e, const char *name, int32_t v, evse_ms_t now)
{
	return evse_param_set(e, evse_param_find(name), v, now);
}

int evse_param_restore(struct evse_config *cfg, uint16_t id, int32_t v)
{
	const struct evse_param *p = evse_param_by_id(id);

	if (p == NULL) {
		return EVSE_PARAM_E_UNKNOWN;
	}
	if (p->kind != EVSE_PK_CONFIG || p->base != CFG) {
		return EVSE_PARAM_E_READONLY;
	}
	int err = check_value(cfg, p, v);

	if (err == EVSE_PARAM_OK) {
		store(p, field_ptr(p, cfg, NULL), v);
	}
	return err;
}

const char *evse_param_strerror(int err)
{
	switch (err) {
	case EVSE_PARAM_OK:
		return "ok";
	case EVSE_PARAM_E_UNKNOWN:
		return "unbekannter Parameter";
	case EVSE_PARAM_E_READONLY:
		return "nur lesbar";
	case EVSE_PARAM_E_RANGE:
		return "Wert ausserhalb des zulaessigen Bereichs";
	case EVSE_PARAM_E_STATE:
		return "im aktuellen Zustand nicht erlaubt";
	case EVSE_PARAM_E_UNAVAILABLE:
		return "Funktion nicht verfuegbar";
	default:
		return "unbekannter Fehler";
	}
}
