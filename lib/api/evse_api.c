/* SPDX-License-Identifier: Apache-2.0 */
/* Abbildung Ladelogik -> API, siehe include/wallfree/api/evse_api.h */
#include <stdint.h>
#include <string.h>
#include <wallfree/api/evse_api.h>

int api_iec61851_state(const struct evse *e)
{
	/* F23: A 0, B 1, C 2, D 3, E/F 4 */
	switch (e->status) {
	case EVSE_CP_A:
		return 0;
	case EVSE_CP_B:
		return 1;
	case EVSE_CP_C:
		return 2;
	case EVSE_CP_D:
		return 3;
	default:
		return 4;
	}
}

int api_charger_state(const struct evse *e)
{
	switch (e->state) {
	case EVSE_S2_LOCKING:
	case EVSE_S3_WAIT:
		return 1;
	case EVSE_S4_SELFTEST:
	case EVSE_S5_PWM_ON:
		return 2;
	case EVSE_S6_CHARGING:
	case EVSE_S7_STOPPING:
		return 3;
	case EVSE_S9_ERROR:
	case EVSE_S10_UNAVAILABLE:
		return 4;
	case EVSE_S8_UNLOCKING:
		/* Entriegeln: mit Fahrzeug "wartet", sonst "nicht verbunden" */
		return e->status == EVSE_CP_A ? 0 : 1;
	default:
		return 0; /* S0, S1 */
	}
}

int api_error_state(const struct evse *e)
{
	if (e->errors == 0) {
		return API_ERR_OK;
	}
	if (e->errors & EVSE_ERR_RCM_ANY) {
		return API_ERR_RCM;
	}
	if (e->errors & EVSE_ERR_CONTACTOR) {
		return API_ERR_CONTACTOR;
	}
	if (e->errors & EVSE_ERR_CLASS_K) {
		return API_ERR_SWITCH;
	}
	return API_ERR_OTHER;
}

int api_contactor_state(const struct evse *e)
{
	/* ohne Spannungsmessung vor/nach dem Schuetz aus dem Sollzustand abgeleitet */
	return e->contactor ? 3 : 0;
}

int api_contactor_error(const struct evse *e)
{
	if (!(e->errors & EVSE_ERR_CONTACTOR)) {
		return 0;
	}
	/* WARP: 2 = sollte geschlossen sein, nach dem Schuetz nichts;
	 *       6 = sollte offen sein, nach dem Schuetz Spannung (geklebt) */
	return e->contactor ? 2 : 6;
}

static void kv(struct wf_jbuf *b, const char *k, int64_t v, bool first)
{
	wf_jb_key(b, k, first);
	wf_jb_int(b, v);
}

void api_json_state(const struct evse *e, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	kv(b, "iec61851_state", api_iec61851_state(e), true);
	kv(b, "charger_state", api_charger_state(e), false);
	kv(b, "contactor_state", api_contactor_state(e), false);
	kv(b, "contactor_error", api_contactor_error(e), false);
	kv(b, "allowed_charging_current", evse_allowed_current(e), false);
	kv(b, "error_state", api_error_state(e), false);
	kv(b, "lock_state", e->lock.state, false);
	wf_jb_raw(b, "}");
}

const char *api_limited_by(const struct evse *e)
{
	const struct evse_current_in *in = &e->cur_in;

	if (e->cur.pwm5_mode) {
		return "pwm5";
	}
	if (e->cur.blocked) {
		return "blocked";
	}
	/* kleinste aktive Grenze; bei Gleichstand wirkt der Sollwert (nichts begrenzt ihn) */
	const char *by = "external";
	int32_t min = in->external_ignored ? INT32_MAX : in->external_ma;
	const struct {
		const char *name;
		int32_t ma;
	} lim[] = {{"hardware", in->hardware_ma}, {"cable", in->cable_ma}, {"input", in->input_ma}};

	for (size_t i = 0; i < sizeof(lim) / sizeof(lim[0]); i++) {
		if (lim[i].ma > 0 && lim[i].ma < min) {
			min = lim[i].ma;
			by = lim[i].name;
		}
	}
	return by;
}

void api_json_limits(const struct evse *e, struct wf_jbuf *b)
{
	const struct evse_current_in *in = &e->cur_in;

	wf_jb_raw(b, "{");
	kv(b, "external", e->rt.external_ma, true);
	wf_jb_key(b, "external_ignored", false);
	wf_jb_raw(b, in->external_ignored ? "true" : "false");
	kv(b, "hardware", in->hardware_ma, false);
	kv(b, "cable", in->cable_ma, false);
	kv(b, "input", in->input_ma, false);
	kv(b, "effective", e->cur.blocked ? 0 : e->cur.i_ma, false);
	kv(b, "allowed", evse_allowed_current(e), false);
	wf_jb_key(b, "limited_by", false);
	wf_jb_str(b, api_limited_by(e));
	wf_jb_raw(b, "}");
}

void api_json_external_current(const struct evse *e, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	kv(b, "current", e->rt.external_ma, true);
	wf_jb_raw(b, "}");
}

struct cur_ctx {
	int64_t v;
	bool found;
};

static int cur_visit(void *ctx, const char *key, const struct wf_jval *v)
{
	struct cur_ctx *c = ctx;

	if (strcmp(key, "current") != 0) {
		return -1; /* nur "current" */
	}
	if (v->type != WF_J_NUM) {
		return -1;
	}
	c->v = v->num;
	c->found = true;
	return 0;
}

int api_parse_current(const char *body, size_t len, int32_t *ma)
{
	char sb[64];
	struct cur_ctx c = {0};
	int64_t v;

	if (wf_json_number(body, len, &v) == 0) {
		c.v = v;
		c.found = true;
	} else if (wf_json_object(body, len, sb, sizeof(sb), cur_visit, &c) != 0 || !c.found) {
		return -1;
	}
	if (c.v < INT32_MIN || c.v > INT32_MAX) {
		return -1;
	}
	*ma = (int32_t)c.v;
	return 0;
}

void api_json_params(const struct evse *e, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);

		kv(b, p->name, evse_param_get(e, p), i == 0);
	}
	wf_jb_raw(b, "}");
}

static const char *const kind_names[] = {"config", "runtime", "status"};

size_t api_json_param_info(size_t idx, struct wf_jbuf *b)
{
	size_t n = evse_param_count();

	if (idx == 0) {
		wf_jb_raw(b, "[");
	}
	for (; idx < n; idx++) {
		const struct evse_param *p = evse_param_at(idx);
		size_t mark = b->len;

		if (idx > 0) {
			wf_jb_raw(b, ",");
		}
		wf_jb_raw(b, "{");
		wf_jb_key(b, "name", true);
		wf_jb_str(b, p->name);
		wf_jb_key(b, "kind", false);
		wf_jb_str(b, kind_names[p->kind]);
		wf_jb_key(b, "unit", false);
		wf_jb_str(b, p->unit);
		if (p->kind != EVSE_PK_STATUS) {
			kv(b, "min", p->min, false);
			kv(b, "max", p->max, false);
			kv(b, "default", p->def, false);
		}
		kv(b, "field", p->orig_field, false);
		wf_jb_key(b, "help", false);
		wf_jb_str(b, p->help);
		wf_jb_raw(b, "}");
		if (b->overflow) {
			/* Eintrag passt nicht mehr: im naechsten Teil */
			b->len = mark;
			b->p[mark] = '\0';
			b->overflow = false;
			return idx;
		}
	}
	size_t mark = b->len;

	wf_jb_raw(b, "]");
	if (b->overflow) {
		b->len = mark;
		b->p[mark] = '\0';
		b->overflow = false;
		return n;
	}
	return n + 1;
}

void api_json_error(struct wf_jbuf *b, const char *text)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "error", true);
	wf_jb_str(b, text);
	wf_jb_raw(b, "}");
}

struct set_ctx {
	struct evse *e;
	evse_ms_t now;
	api_written_t written;
	void *wctx;
	struct wf_jbuf *out;
	int errors;
};

static void set_error(struct set_ctx *c, const char *key, const char *text)
{
	if (c->errors == 0) {
		wf_jb_raw(c->out, "{\"errors\":{");
	}
	wf_jb_key(c->out, key, c->errors == 0);
	wf_jb_str(c->out, text);
	c->errors++;
}

static int set_visit(void *ctx, const char *key, const struct wf_jval *v)
{
	struct set_ctx *c = ctx;
	const struct evse_param *p = evse_param_find(key);

	if (p == NULL) {
		set_error(c, key, evse_param_strerror(EVSE_PARAM_E_UNKNOWN));
		return 0;
	}
	if ((v->type != WF_J_NUM && v->type != WF_J_BOOL) || v->num < INT32_MIN ||
	    v->num > INT32_MAX) {
		set_error(c, key, "Zahl erwartet");
		return 0;
	}
	int err = evse_param_set(c->e, p, (int32_t)v->num, c->now);

	if (err != EVSE_PARAM_OK) {
		set_error(c, key, evse_param_strerror(err));
		return 0;
	}
	if (c->written != NULL) {
		c->written(c->wctx, p, (int32_t)v->num);
	}
	return 0;
}

static int syntax_only(void *ctx, const char *key, const struct wf_jval *v)
{
	(void)ctx;
	(void)key;
	(void)v;
	return 0;
}

int api_set_params(struct evse *e, const char *body, size_t len, evse_ms_t now,
		   api_written_t written, void *ctx, struct wf_jbuf *out)
{
	char sb[512];
	struct set_ctx c = {.e = e, .now = now, .written = written, .wctx = ctx, .out = out};

	/* erst die Syntax pruefen, damit ein Fehler am Ende nichts halb anwendet */
	if (wf_json_object(body, len, sb, sizeof(sb), syntax_only, NULL) != 0) {
		api_json_error(out, "ungültiges JSON (erwartet: flaches Objekt)");
		return 400;
	}
	(void)wf_json_object(body, len, sb, sizeof(sb), set_visit, &c);
	if (c.errors > 0) {
		wf_jb_raw(out, "}}");
		return 400;
	}
	wf_jb_raw(out, "{}");
	return 200;
}

/* ------------------------------------------------ Fehler (SOLL-ERR-01) */

static const char *const err_names[] = {
	"CABLE-REJECT", "PP-INVALID", "CP-INVALID", "LOCK", "UNLOCK", "LD-LOST", "OVERCURRENT",
	"METER-COMM", "STATE-D", "CONTACTOR", "DIODE", "INPUT", "RCM-SELFTEST", "RCM-SENSOR",
	"RCM-DC6", "RCM-AC30", "SWITCH", "CONFIG", "INTERNAL",
};

static const char *const err_reset[] = {
	"anderer Kabelwert", "Bedingung entfällt", "CP-Pegel >= 1,2 V", "Erfolg, CP A",
	"Erfolg, CP A", "Wiederverriegeln, CP A", "CP A", "Messquelle wieder gültig", "CP A",
	"Quittierung (F14)", "CP A", "Eingang inaktiv", "Selbsttest bestanden",
	"Selbsttest bestanden", "Selbsttest bestanden", "Selbsttest bestanden", "Neustart",
	"Parameterkorrektur bzw. Neustart", "Neustart",
};

#define N_ERR (sizeof(err_names) / sizeof(err_names[0]))

const char *api_err_name(unsigned int bit)
{
	return bit < N_ERR ? err_names[bit] : NULL;
}

const char *api_err_reset(unsigned int bit)
{
	return bit < N_ERR ? err_reset[bit] : NULL;
}

void api_json_err_list(struct wf_jbuf *b, evse_err_t m)
{
	bool first = true;

	wf_jb_raw(b, "[");
	for (unsigned int i = 0; i < N_ERR; i++) {
		if (m & (1u << i)) {
			if (!first) {
				wf_jb_raw(b, ",");
			}
			wf_jb_str(b, err_names[i]);
			first = false;
		}
	}
	wf_jb_raw(b, "]");
}

void api_json_errors(const struct evse *e, evse_ms_t now, struct wf_jbuf *b)
{
	wf_jb_raw(b, "{");
	wf_jb_key(b, "active", true);
	api_json_err_list(b, e->errors);
	wf_jb_key(b, "seen", false);
	api_json_err_list(b, e->errors_seen);
	wf_jb_key(b, "history", false);
	wf_jb_raw(b, "[");
	bool truncated = false;

	for (size_t i = 0; i < evse_err_hist_count(e); i++) {
		const struct evse_err_event *ev = evse_err_hist_at(e, i);
		size_t mark = b->len;

		wf_jb_raw(b, i == 0 ? "{" : ",{");
		kv(b, "age_ms", now - ev->t, true);
		kv(b, "state", ev->state, false);
		wf_jb_key(b, "set", false);
		api_json_err_list(b, ev->set);
		wf_jb_key(b, "cleared", false);
		api_json_err_list(b, ev->cleared);
		wf_jb_raw(b, "}");
		/* Platz fuer den Abschluss lassen; aeltere Eintraege entfallen */
		if (b->overflow || b->size - b->len < 24) {
			b->len = mark;
			b->p[mark] = '\0';
			b->overflow = false;
			truncated = true;
			break;
		}
	}
	wf_jb_raw(b, truncated ? "],\"truncated\":true}" : "]}");
}
