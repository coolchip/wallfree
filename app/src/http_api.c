/* SPDX-License-Identifier: Apache-2.0 */
/*
 * HTTP- und WebSocket-API (Soll-Spezifikation 12.1, SOLL-API-01..07/30,
 * Zaehler SOLL-MTR-30/31, MQTT-Status SOLL-MQTT-30, Konsole SOLL-SH-19), Umfang fuer evcc
 * ueber die Vorlage tinkerforge-warp-ws.
 *
 *  - Alle Ressourcen mit HTTP-Digest (MD5, qop=auth). Im Werkszugang
 *    beantwortet die API nur /credentials, sonst 403.
 *  - WebSocket /ws: Anmeldung und freier Platz werden vor dem Upgrade (101)
 *    geprueft (Zephyr-Patch, ws_upgrade_check); abgewiesen wird mit 401
 *    (frische Challenge), 403, 429 oder 503 wie bei den uebrigen Ressourcen.
 *  - Schreibzugriffe laufen ueber die Parametertabelle mit Grenzpruefung (E14).
 *
 * Alle Ressourcen-Callbacks laufen im Thread des Zephyr-HTTP-Servers. Nur
 * gebaut, auf Hardware nicht ausgefuehrt.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/websocket.h>
#include <wallfree/api/alog.h>
#include <wallfree/api/events.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/api/history.h>
#include <wallfree/api/meter_json.h>
#include <wallfree/api/ratelimit.h>
#include <wallfree/evse/persist.h>
#include "auth.h"
#include "evse_task.h"
#include "history_task.h"
#include "hw.h"
#include "meter_task.h"
#include "mqtt_task.h"
#include "netcfg.h"
#include "http_api.h"
#include "shell_web.h"
#include "storage.h"
#include "sysinfo.h"

LOG_MODULE_REGISTER(http_api, LOG_LEVEL_INF);

#define HTTP_PORT      CONFIG_WALLFREE_HTTP_PORT
#define BODY_MAX       1024
#define OUT_MAX        2048  /* auch Ausgabe der Konsole (SOLL-SH-19) */
#define WS_MAX         3     /* SOLL-API-07: mindestens 2 */
#define WS_PERIOD_MS   100
#define WS_STACK       2048
#define WS_PRIO        11
#define WS_SEND_MS     200
#define WS_DRAIN_MS    2
#define WS_BEAT_MS     2000  /* evse/state auch ohne Aenderung: Lebenszeichen fuer Clients */

HTTP_SERVER_REGISTER_HEADER_CAPTURE(capture_auth, "Authorization");

static char out[OUT_MAX];          /* Antwortpuffer (nur Server-Thread) */
static char challenge[160];
static struct http_header hdr_auth = {.name = "WWW-Authenticate", .value = challenge};
static char retry_after[12];
static struct http_header hdr_retry = {.name = "Retry-After", .value = retry_after};
static struct wf_rl rl; /* Bremse je Absender (nur Server-Thread) */
/* Ereignisprotokoll (SOLL-ERR-11), Server- und WebSocket-Thread; uebersteht Neustarts
 * im Backup-SRAM (SOLL-ERR-12) */
static struct wf_alog alog HW_RETAINED;
static K_MUTEX_DEFINE(alog_mtx);

/* IPv4-Absender eines Sockets, 0 wenn unbekannt */
static uint32_t peer_addr(int fd)
{
	struct net_sockaddr_storage ss;
	net_socklen_t len = sizeof(ss);

	if (zsock_getpeername(fd, (struct net_sockaddr *)&ss, &len) != 0 ||
	    ss.ss_family != NET_AF_INET) {
		return 0;
	}
	return net_ntohl(net_sin((struct net_sockaddr *)&ss)->sin_addr.s_addr);
}

/*
 * Anmeldung pruefen mit Bremse: gesperrte Absender werden auch mit
 * richtigem Passwort abgewiesen (sonst bremst die Sperre nichts).
 * @return Ergebnis; *wait_ms > 0, wenn gesperrt
 */
static enum wf_auth checked_auth(uint32_t addr, const char *hdr, const char *method,
				 const char *uri, int64_t *wait_ms)
{
	int64_t now = k_uptime_get();

	*wait_ms = wf_rl_blocked(&rl, addr, now);
	if (*wait_ms > 0) {
		return WF_AUTH_BAD;
	}
	enum wf_auth a = auth_check(hdr, method, uri);

	if (a == WF_AUTH_BAD) {
		wf_rl_fail(&rl, addr, now);
		k_mutex_lock(&alog_mtx, K_FOREVER);
		wf_alog_auth_fail(&alog, addr, now);
		k_mutex_unlock(&alog_mtx);
		LOG_WRN("Anmeldung fehlgeschlagen von %u.%u.%u.%u", addr >> 24, (addr >> 16) & 255,
			(addr >> 8) & 255, addr & 255);
	} else if (a == WF_AUTH_OK) {
		wf_rl_ok(&rl, addr);
	}
	return a;
}

/* ------------------------------------------------ gemeinsamer Ablauf */

struct res;
struct web_file {
	const uint8_t *data;
	size_t len;
};
typedef void (*res_fn_t)(struct http_client_ctx *client, struct res *r,
			 struct http_response_ctx *rsp);

struct res {
	res_fn_t fn;
	bool allow_factory;  /* auch im Werkszugang (nur /credentials) */
	/* Zustand der laufenden Anfrage */
	bool decided;
	bool responded;
	bool too_long;
	size_t body_len;
	size_t cursor;       /* fuer Antworten in Teilen */
	char *body;          /* nur POST-Ressourcen, mit Platz fuer NUL */
	size_t body_size;
	const struct web_file *file; /* nur Dateien der Oberflaeche */
};

static const char *find_header(const struct http_request_ctx *req, const char *name)
{
	for (size_t i = 0; i < req->header_count; i++) {
		if (strcasecmp(req->headers[i].name, name) == 0) {
			return req->headers[i].value;
		}
	}
	return NULL;
}

static void respond(struct http_response_ctx *rsp, enum http_status status, size_t len)
{
	rsp->status = status;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = len;
	rsp->final_chunk = true;
}

static void respond_json(struct http_response_ctx *rsp, enum http_status status,
			 const struct wf_jbuf *b)
{
	if (b->overflow) {
		int n = snprintf(out, sizeof(out), "{\"error\":\"Antwort zu gross\"}");

		respond(rsp, HTTP_500_INTERNAL_SERVER_ERROR, (size_t)n);
		return;
	}
	respond(rsp, status, b->len);
}

static void respond_error(struct http_response_ctx *rsp, enum http_status status,
			  const char *text)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_error(&b, text);
	respond_json(rsp, status, &b);
}

static bool gate(struct http_client_ctx *client, struct res *r,
		 const struct http_request_ctx *req, struct http_response_ctx *rsp)
{
	const char *method = http_method_str(client->method);
	int64_t wait_ms;
	enum wf_auth a = checked_auth(peer_addr(client->fd), find_header(req, "Authorization"),
				      method, (const char *)client->url_buffer, &wait_ms);

	if (wait_ms > 0) {
		snprintf(retry_after, sizeof(retry_after), "%u", (unsigned int)(wait_ms / 1000 + 1));
		rsp->headers = &hdr_retry;
		rsp->header_count = 1;
		respond_error(rsp, HTTP_429_TOO_MANY_REQUESTS,
			      "zu viele Fehlversuche, später erneut versuchen");
		return false;
	}

	if (req->headers_status == HTTP_HEADER_STATUS_DROPPED && a != WF_AUTH_OK) {
		LOG_WRN("Authorization-Kopfzeile zu lang");
	}
	if (a != WF_AUTH_OK) {
		if (auth_challenge(a == WF_AUTH_STALE, challenge, sizeof(challenge)) < 0) {
			respond_error(rsp, HTTP_500_INTERNAL_SERVER_ERROR, "Challenge");
			return false;
		}
		rsp->headers = &hdr_auth;
		rsp->header_count = 1;
		respond_error(rsp, HTTP_401_UNAUTHORIZED, "Anmeldung erforderlich");
		return false;
	}
	if (auth_must_change() && !r->allow_factory) {
		respond_error(rsp, HTTP_403_FORBIDDEN,
			      "Werkszugang: zuerst Zugangsdaten ändern (POST /credentials)");
		return false;
	}
	return true;
}

static int dyn_step(struct http_client_ctx *client, enum http_transaction_status status,
		    const struct http_request_ctx *req, struct http_response_ctx *rsp,
		    void *user_data)
{
	struct res *r = user_data;

	if (status == HTTP_SERVER_TRANSACTION_ABORTED ||
	    status == HTTP_SERVER_TRANSACTION_COMPLETE) {
		r->decided = false;
		r->responded = false;
		r->too_long = false;
		r->body_len = 0;
		r->cursor = 0;
		return 0;
	}
	if (!r->decided) {
		/* Kopfzeilen gibt es nur im ersten Aufruf */
		r->decided = true;
		if (!gate(client, r, req, rsp)) {
			r->responded = true;
			return 0;
		}
	}
	if (r->responded) {
		rsp->final_chunk = true; /* restlichen Koerper verwerfen */
		return 0;
	}
	if (client->method == HTTP_POST && req->data_len > 0) {
		if (r->body_len + req->data_len >= r->body_size) {
			r->too_long = true;
		} else {
			memcpy(r->body + r->body_len, req->data, req->data_len);
			r->body_len += req->data_len;
		}
	}
	if (status != HTTP_SERVER_REQUEST_DATA_FINAL) {
		return 0;
	}
	r->body[r->body_len] = '\0';
	if (r->too_long) {
		r->responded = true;
		respond_error(rsp, HTTP_413_PAYLOAD_TOO_LARGE, "Anfrage zu gross");
		return 0;
	}
	r->fn(client, r, rsp);
	return 0;
}

/* SOLL-RT-11: jeder Aufruf wird auf seine Dauer ueberwacht */
static int dyn_cb(struct http_client_ctx *client, enum http_transaction_status status,
		  const struct http_request_ctx *req, struct http_response_ctx *rsp,
		  void *user_data)
{
	evse_task_http_busy(true);
	int ret = dyn_step(client, status, req, rsp, user_data);

	evse_task_http_busy(false);
	return ret;
}

#define JSON_RESOURCE(_id, _path, _methods, _fn, _factory, _body)                                  \
	static char _id##_body[(_body) + 1];                                                       \
	static struct res _id##_res = {.fn = _fn, .allow_factory = _factory,                       \
				       .body = _id##_body, .body_size = sizeof(_id##_body)};      \
	static struct http_resource_detail_dynamic _id##_detail = {                                \
		.common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                     \
			   .bitmask_of_supported_http_methods = (_methods),                        \
			   .content_type = "application/json"},                                    \
		.cb = dyn_cb,                                                                      \
		.user_data = &_id##_res,                                                           \
	};                                                                                         \
	HTTP_RESOURCE_DEFINE(_id, wallfree_http, _path, &_id##_detail)

static uint16_t http_port = HTTP_PORT;
HTTP_SERVICE_DEFINE(wallfree_http, NULL, &http_port, CONFIG_HTTP_SERVER_MAX_CLIENTS, 4, NULL, NULL,
		    NULL);

/* ------------------------------------------------ Weboberflaeche (SOLL-API-31) */

static const uint8_t index_gz[] = {
#include "web/index.html.gz.inc"
};
static const uint8_t app_gz[] = {
#include "web/app.js.gz.inc"
};
static const uint8_t style_gz[] = {
#include "web/style.css.gz.inc"
};
static const uint8_t vendor_gz[] = {
#include "web/preact-htm.js.gz.inc"
};
static const uint8_t licenses_gz[] = {
#include "web/lizenzen.txt.gz.inc"
};

static const struct http_header web_headers[] = {
	{.name = "Content-Encoding", .value = "gzip"},
	{.name = "Cache-Control", .value = "no-cache"},
};

static void web_file(struct http_client_ctx *client, struct res *r, struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	rsp->status = HTTP_200_OK;
	rsp->headers = web_headers;
	rsp->header_count = ARRAY_SIZE(web_headers);
	rsp->body = r->file->data;
	rsp->body_len = r->file->len;
	rsp->final_chunk = true;
}

/* hinter derselben Anmeldung; auch im Werkszugang (Passwortaenderung) */
#define WEB_RESOURCE(_id, _path, _data, _type)                                                     \
	static const struct web_file _id##_file = {.data = _data, .len = sizeof(_data)};           \
	static char _id##_body[1];                                                                 \
	static struct res _id##_res = {.fn = web_file, .allow_factory = true,                      \
				       .body = _id##_body, .body_size = 1, .file = &_id##_file};  \
	static struct http_resource_detail_dynamic _id##_detail = {                                \
		.common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,                                     \
			   .bitmask_of_supported_http_methods = BIT(HTTP_GET),                     \
			   .content_type = _type},                                                 \
		.cb = dyn_cb,                                                                      \
		.user_data = &_id##_res,                                                           \
	};                                                                                         \
	HTTP_RESOURCE_DEFINE(_id, wallfree_http, _path, &_id##_detail)

WEB_RESOURCE(web_index, "/", index_gz, "text/html; charset=utf-8");
WEB_RESOURCE(web_app, "/app.js", app_gz, "text/javascript");
WEB_RESOURCE(web_style, "/style.css", style_gz, "text/css");
WEB_RESOURCE(web_vendor, "/vendor.js", vendor_gz, "text/javascript");
WEB_RESOURCE(web_licenses, "/lizenzen.txt", licenses_gz, "text/plain; charset=utf-8");

/* ------------------------------------------------ /info (SOLL-API-01/02) */

static void info_name(struct http_client_ctx *client, struct res *r,
		      struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;
	char uid[7];

	netcfg_uid(uid);
	wf_jb_init(&b, out, sizeof(out));
	wf_jb_raw(&b, "{");
	wf_jb_key(&b, "name", true);
	wf_jb_str(&b, net_hostname_get());
	/* F22: Typ warp2, damit evcc keine Phasenumschaltung anspricht */
	wf_jb_key(&b, "type", false);
	wf_jb_str(&b, "warp2");
	wf_jb_key(&b, "display_type", false);
	wf_jb_str(&b, "Wallfree");
	wf_jb_key(&b, "uid", false);
	wf_jb_str(&b, uid);
	wf_jb_raw(&b, "}");
	respond_json(rsp, HTTP_200_OK, &b);
}

static void info_features(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	/* SOLL-API-02: Zaehler nur, wenn konfiguriert; kein NFC, keine Phasenumschaltung.
	 * evcc liest das nur beim Verbinden. */
	strcpy(out, meter_task_type() != METER_NONE ? "[\"meters\"]" : "[]");
	respond(rsp, HTTP_200_OK, strlen(out));
}

JSON_RESOURCE(info_name_r, "/info/name", BIT(HTTP_GET), info_name, false, 0);
JSON_RESOURCE(info_features_r, "/info/features", BIT(HTTP_GET), info_features, false, 0);

/* ------------------------------------------------ /evse (SOLL-API-03..05/30) */

static K_SEM_DEFINE(ws_wake, 0, 1);

void http_api_notify(void)
{
	k_sem_give(&ws_wake);
}

static void evse_state(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_state(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void external_current(struct http_client_ctx *client, struct res *r,
			     struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	if (client->method == HTTP_POST) {
		int32_t ma;

		if (api_parse_current(r->body, r->body_len, &ma) != 0) {
			respond_error(rsp, HTTP_400_BAD_REQUEST,
				      "erwartet {\"current\": mA} oder mA als Zahl");
			return;
		}
		/* Slot external, nur RAM (SOLL-NVM-03); 0 = Laden aus */
		int err = evse_param_set_by_name(evse_acquire(), "external_current", ma,
						 k_uptime_get());

		evse_release();
		if (err != EVSE_PARAM_OK) {
			respond_error(rsp, HTTP_400_BAD_REQUEST, evse_param_strerror(err));
			return;
		}
		k_sem_give(&ws_wake);
	}
	wf_jb_init(&b, out, sizeof(out));
	api_json_external_current(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void persist_written(void *ctx, const struct evse_param *p, int32_t v)
{
	int *pending = ctx;

	/* Lock-Reihenfolge Ladelogik -> Speicher wie in storage.c */
	if (storage_save_param(p, v) == NVM_PENDING) {
		(*pending)++;
	}
}

static void evse_params(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	if (client->method == HTTP_POST) {
		int pending = 0;
		struct evse *e = evse_acquire();
		int st = api_set_params(e, r->body, r->body_len, k_uptime_get(), persist_written,
					&pending, &b);

		evse_release();
		k_sem_give(&ws_wake);
		respond_json(rsp, st == 200 ? HTTP_200_OK : HTTP_400_BAD_REQUEST, &b);
		return;
	}
	api_json_params(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

static void param_info(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	/* in Teilen: der Server ruft erneut auf, bis final_chunk gesetzt ist */
	wf_jb_init(&b, out, sizeof(out));
	r->cursor = api_json_param_info(r->cursor, &b);
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor > evse_param_count();
}

static void evse_errors(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_errors(evse_acquire(), k_uptime_get(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

/* Sollwert, Grenzen, wirksamer Strom (eigene Erweiterung) */
static void evse_limits(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	api_json_limits(evse_acquire(), &b);
	evse_release();
	respond_json(rsp, HTTP_200_OK, &b);
}

JSON_RESOURCE(evse_limits_r, "/evse/limits", BIT(HTTP_GET), evse_limits, false, 0);
JSON_RESOURCE(evse_errors_r, "/evse/errors", BIT(HTTP_GET), evse_errors, false, 0);

/* SOLL-ERR-03: nur der Schuetzfehler wird quittiert; andere loeschen sich selbst */
static void evse_errors_ack(struct http_client_ctx *client, struct res *r,
			    struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct evse *e = evse_acquire();
	bool had = (e->errors & EVSE_ERR_CONTACTOR) != 0;
	bool ok = had && evse_ack_contactor_fault(e);

	evse_release();
	if (had && !ok) {
		respond_error(rsp, HTTP_409_CONFLICT,
			      "Schützfehler nicht quittierbar (Ursache besteht oder falscher Zustand)");
		return;
	}
	k_sem_give(&ws_wake);
	strcpy(out, ok ? "{\"acknowledged\":true}" : "{\"acknowledged\":false}");
	respond(rsp, HTTP_200_OK, strlen(out));
}

JSON_RESOURCE(evse_errors_ack_r, "/evse/errors/ack", BIT(HTTP_POST), evse_errors_ack, false, 16);
JSON_RESOURCE(evse_state_r, "/evse/state", BIT(HTTP_GET), evse_state, false, 0);
JSON_RESOURCE(external_current_r, "/evse/external_current", BIT(HTTP_GET) | BIT(HTTP_POST),
	      external_current, false, 64);
JSON_RESOURCE(evse_params_r, "/evse/params", BIT(HTTP_GET) | BIT(HTTP_POST), evse_params,
	      false, BODY_MAX);
JSON_RESOURCE(param_info_r, "/evse/param_info", BIT(HTTP_GET), param_info, false, 0);

/* ------------------------------------------------ /meters/0 (SOLL-MTR-30/31) */

enum meter_topic {
	MT_VALUE_IDS = 0,
	MT_VALUES,
	MT_STATE,
	MT_COUNT,
};
static const char *const meter_topic[MT_COUNT] = {"meters/0/value_ids", "meters/0/values",
						 "meters/0/state"};

static void meter_render(enum meter_topic t, struct wf_jbuf *b)
{
	struct meter_values mv;

	if (t == MT_VALUE_IDS) {
		wf_meter_value_ids_json(meter_task_type(), b);
		return;
	}
	(void)meter_task_get(&mv);
	if (t == MT_VALUES) {
		wf_meter_values_json(&mv, b);
	} else {
		wf_meter_state_json(&mv, b);
	}
}

static void meter_res(struct http_response_ctx *rsp, enum meter_topic t)
{
	struct wf_jbuf b;

	if (meter_task_type() == METER_NONE) {
		respond_error(rsp, HTTP_404_NOT_FOUND, "kein Zähler konfiguriert (meter_type)");
		return;
	}
	wf_jb_init(&b, out, sizeof(out));
	meter_render(t, &b);
	respond_json(rsp, HTTP_200_OK, &b);
}

#define METER_RESOURCE(_id, _path, _topic)                                                         \
	static void _id##_fn(struct http_client_ctx *client, struct res *r,                        \
			     struct http_response_ctx *rsp)                                        \
	{                                                                                          \
		ARG_UNUSED(client);                                                                \
		ARG_UNUSED(r);                                                                     \
		meter_res(rsp, _topic);                                                            \
	}                                                                                          \
	JSON_RESOURCE(_id, _path, BIT(HTTP_GET), _id##_fn, false, 0)

METER_RESOURCE(meter_ids_r, "/meters/0/value_ids", MT_VALUE_IDS);
METER_RESOURCE(meter_values_r, "/meters/0/values", MT_VALUES);
METER_RESOURCE(meter_state_r, "/meters/0/state", MT_STATE);

/* ------------------------------------------------ /credentials (SOLL-API-07) */

struct cred_ctx {
	char user[WF_DIGEST_USER_MAX + 1];
	char password[AUTH_PW_MAX + 1];
	bool bad;
};

static int cred_visit(void *ctx, const char *key, const struct wf_jval *v)
{
	struct cred_ctx *c = ctx;
	char *dst;
	size_t size;

	if (strcmp(key, "user") == 0) {
		dst = c->user;
		size = sizeof(c->user);
	} else if (strcmp(key, "password") == 0) {
		dst = c->password;
		size = sizeof(c->password);
	} else {
		c->bad = true;
		return 0;
	}
	if (v->type != WF_J_STR || v->str_len >= size) {
		c->bad = true;
		return 0;
	}
	memcpy(dst, v->str, v->str_len + 1);
	return 0;
}

static void credentials(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	if (client->method == HTTP_POST) {
		struct cred_ctx c = {0};
		char sb[160];

		if (wf_json_object(r->body, r->body_len, sb, sizeof(sb), cred_visit, &c) != 0 ||
		    c.bad || c.user[0] == '\0' || c.password[0] == '\0') {
			respond_error(rsp, HTTP_400_BAD_REQUEST,
				      "erwartet {\"user\": Text, \"password\": Text}");
			return;
		}
		const char *err = auth_change(c.user, c.password);

		memset(&c, 0, sizeof(c));
		memset(r->body, 0, r->body_size);
		if (err != NULL) {
			respond_error(rsp, HTTP_400_BAD_REQUEST, err);
			return;
		}
	}
	wf_jb_init(&b, out, sizeof(out));
	wf_jb_raw(&b, "{");
	wf_jb_key(&b, "must_change", true);
	wf_jb_raw(&b, auth_must_change() ? "true" : "false");
	wf_jb_key(&b, "user", false);
	wf_jb_str(&b, auth_user());
	wf_jb_raw(&b, "}");
	respond_json(rsp, HTTP_200_OK, &b);
}

JSON_RESOURCE(credentials_r, "/credentials", BIT(HTTP_GET) | BIT(HTTP_POST), credentials, true, 256);

/* ------------------------------------------------ /system (10.1 net_*, Konsole) */

static void system_config(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	if (client->method == HTTP_POST) {
		/* alle Werte gueltig oder keiner; wirkt nach Neustart */
		int st = netcfg_set_json(r->body, r->body_len, &b);

		respond_json(rsp, st == 200 ? HTTP_200_OK : HTTP_400_BAD_REQUEST, &b);
		return;
	}
	struct wf_sysconf a, sv;

	netcfg_get(&a, &sv);
	wf_jb_raw(&b, "{\"active\":");
	wf_sys_json(&a, &b);
	wf_jb_raw(&b, ",\"saved\":");
	wf_sys_json(&sv, &b);
	wf_jb_raw(&b, netcfg_restart_required() ? ",\"restart_required\":true}"
						 : ",\"restart_required\":false}");
	respond_json(rsp, HTTP_200_OK, &b);
}

static void system_config_info(struct http_client_ctx *client, struct res *r,
			       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	/* in Teilen: der Server ruft erneut auf, bis final_chunk gesetzt ist */
	wf_jb_init(&b, out, sizeof(out));
	r->cursor = wf_sys_info_json(r->cursor, &b);
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor > wf_sys_count();
}

static void system_restart(struct http_client_ctx *client, struct res *r,
			   struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	/* SOLL-RT-22: laufendes Laden endet geordnet, hoechstens 15 s */
	(void)evse_task_reboot_orderly(WF_RST_USER);
	strcpy(out, "{}");
	respond(rsp, HTTP_202_ACCEPTED, 2);
}

/* SOLL-BOOT-30..32: Uebergabe an den Bootloader; den Upload macht die Weboberflaeche */
static void system_update(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	char addr[16];
	uint32_t ip, mask;

	netcfg_bootloader_addr(addr, &ip, &mask);
	if (evse_task_update_orderly(ip, mask) != 0) {
		respond_error(rsp, HTTP_409_CONFLICT, "Neustart oder Update läuft bereits");
		return;
	}
	int n = snprintf(out, sizeof(out), "{\"address\":\"%s\",\"wait_s\":120,\"upload_s\":30}",
			 addr);

	respond(rsp, HTTP_202_ACCEPTED, n);
}

static void system_info(struct http_client_ctx *client, struct res *r,
			struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	sysinfo_json(&b);
	respond_json(rsp, HTTP_200_OK, &b);
}

/* SOLL-ERR-11: in Teilen, der Server ruft erneut auf, bis final_chunk gesetzt ist */
static void system_log(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	k_mutex_lock(&alog_mtx, K_FOREVER);
	uint32_t next = wf_alog_json(&alog, (uint32_t)r->cursor, &b);

	k_mutex_unlock(&alog_mtx);
	r->cursor = next;
	rsp->status = HTTP_200_OK;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = next == WF_ALOG_DONE;
}

/* SOLL-ERR-13: Ereignisprotokoll loeschen; der erste neue Eintrag nennt den Absender */
static void system_log_clear(struct http_client_ctx *client, struct res *r,
			     struct http_response_ctx *rsp)
{
	ARG_UNUSED(r);
	uint32_t addr = peer_addr(client->fd);

	k_mutex_lock(&alog_mtx, K_FOREVER);
	wf_alog_clear(&alog, addr, k_uptime_get());
	k_mutex_unlock(&alog_mtx);
	LOG_INF("Ereignisprotokoll geloescht von %u.%u.%u.%u", addr >> 24, (addr >> 16) & 255, (addr >> 8) & 255,
		addr & 255);
	strcpy(out, "{}");
	respond(rsp, HTTP_200_OK, 2);
}

/* SOLL-API-33: in Teilen wie /system/log */
static void evse_history(struct http_client_ctx *client, struct res *r,
			 struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	r->cursor = history_task_json((uint32_t)r->cursor, &b);
	rsp->status = HTTP_200_OK;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor == WF_HIST_DONE;
}

/*
 * SOLL-API-34: CP-Aufnahme (eigene Erweiterung). Teil 1 nimmt auf (etwa 120 ms) und sendet
 * Kopf und CP+-Kanal, Teil 2 den CP--Kanal. Punkte im festen Raster t0 + i * dt.
 */
static struct hw_scope scope; /* nur Server-Thread */

static void scope_array(struct wf_jbuf *b, const int16_t *v)
{
	wf_jb_raw(b, "[");
	for (int i = 0; i < HW_SCOPE_N; i++) {
		if (i > 0) {
			wf_jb_raw(b, ",");
		}
		wf_jb_int(b, v[i]);
	}
	wf_jb_raw(b, "]");
}

static void evse_cp_scope(struct http_client_ctx *client, struct res *r,
			  struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	if (r->cursor == 0) {
		int e = hw_cp_scope(&scope);

		if (e != 0) {
			respond_error(rsp, HTTP_409_CONFLICT,
				      e == -EBUSY ? "Aufnahme läuft bereits" : "Aufnahme fehlgeschlagen");
			return;
		}
		wf_jb_raw(&b, "{");
		wf_jb_key(&b, "duty", true);
		wf_jb_int(&b, scope.duty);
		wf_jb_key(&b, "t0_us", false);
		wf_jb_int(&b, HW_SCOPE_T0_US);
		wf_jb_key(&b, "dt_us", false);
		wf_jb_int(&b, -HW_SCOPE_DT_US);
		wf_jb_key(&b, "pos_mv", false);
		scope_array(&b, scope.pos_mv);
		r->cursor = 1;
	} else {
		wf_jb_raw(&b, ",");
		wf_jb_key(&b, "neg_mv", true);
		scope_array(&b, scope.neg_mv);
		wf_jb_raw(&b, "}");
		r->cursor = 0;
	}
	rsp->status = HTTP_200_OK;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor == 0;
}

JSON_RESOURCE(evse_cp_scope_r, "/evse/cp_scope", BIT(HTTP_GET), evse_cp_scope, false, 0);
JSON_RESOURCE(evse_history_r, "/evse/history", BIT(HTTP_GET), evse_history, false, 0);
JSON_RESOURCE(system_log_r, "/system/log", BIT(HTTP_GET), system_log, false, 0);
JSON_RESOURCE(system_log_clear_r, "/system/log/clear", BIT(HTTP_POST), system_log_clear, false, 16);
JSON_RESOURCE(system_info_r, "/system/info", BIT(HTTP_GET), system_info, false, 0);
JSON_RESOURCE(system_config_r, "/system/config", BIT(HTTP_GET) | BIT(HTTP_POST), system_config,
	      false, 512);
JSON_RESOURCE(system_config_info_r, "/system/config_info", BIT(HTTP_GET), system_config_info,
	      false, 0);
JSON_RESOURCE(system_restart_r, "/system/restart", BIT(HTTP_POST), system_restart, false, 16);
JSON_RESOURCE(system_update_r, "/system/update", BIT(HTTP_POST), system_update, false, 16);

/* SOLL-SH-19: Konsole, eine Kommandozeile je Anfrage, Ausgabe als Text */
#define SHELL_LINE_MAX (CONFIG_SHELL_CMD_BUFF_SIZE - 1)
#define SHELL_CUT_NOTE "\n[Ausgabe gekürzt]\n"

static void system_shell(struct http_client_ctx *client, struct res *r,
			 struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	bool cut;

	while (r->body_len > 0 && (r->body[r->body_len - 1] == '\n' ||
				   r->body[r->body_len - 1] == '\r')) {
		r->body[--r->body_len] = '\0';
	}
	int n = shell_web_exec(r->body, out, sizeof(out) - sizeof(SHELL_CUT_NOTE) + 1, &cut);

	if (n < 0) {
		n = snprintf(out, sizeof(out), "Konsole belegt\n");
		respond(rsp, HTTP_409_CONFLICT, (size_t)n);
		return;
	}
	if (cut) {
		strcpy(out + n, SHELL_CUT_NOTE);
		n += sizeof(SHELL_CUT_NOTE) - 1;
	}
	respond(rsp, HTTP_200_OK, (size_t)n);
}

/* SOLL-SH-23: Kommandobaum fuer die Vervollstaendigung, in Teilen */
static void system_shell_commands(struct http_client_ctx *client, struct res *r,
				  struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	struct wf_jbuf b;

	wf_jb_init(&b, out, sizeof(out));
	r->cursor = shell_web_commands_json(r->cursor, &b);
	rsp->status = HTTP_200_OK;
	rsp->body = (const uint8_t *)out;
	rsp->body_len = b.len;
	rsp->final_chunk = r->cursor == SHELL_WEB_DONE;
}

JSON_RESOURCE(system_shell_commands_r, "/system/shell_commands", BIT(HTTP_GET),
	      system_shell_commands, false, 0);

static char system_shell_body[SHELL_LINE_MAX + 1];
static struct res system_shell_res = {.fn = system_shell, .body = system_shell_body,
				      .body_size = sizeof(system_shell_body)};
static struct http_resource_detail_dynamic system_shell_detail = {
	.common = {.type = HTTP_RESOURCE_TYPE_DYNAMIC,
		   .bitmask_of_supported_http_methods = BIT(HTTP_POST),
		   .content_type = "text/plain; charset=utf-8"},
	.cb = dyn_cb,
	.user_data = &system_shell_res,
};
HTTP_RESOURCE_DEFINE(system_shell_r, wallfree_http, "/system/shell", &system_shell_detail);

/* ------------------------------------------------ /mqtt/state (SOLL-MQTT-30) */

static void mqtt_state(struct http_client_ctx *client, struct res *r,
		       struct http_response_ctx *rsp)
{
	ARG_UNUSED(client);
	ARG_UNUSED(r);
	struct wf_mq_status s;
	struct wf_jbuf b;

	mqtt_task_status(&s);
	wf_jb_init(&b, out, sizeof(out));
	wf_mq_status_json(&s, &b);
	respond_json(rsp, HTTP_200_OK, &b);
}

JSON_RESOURCE(mqtt_state_r, "/mqtt/state", BIT(HTTP_GET), mqtt_state, false, 0);

/* ------------------------------------------------ WebSocket /ws (SOLL-API-06) */

static struct {
	int sock;           /* -1 = frei */
	bool full_sync;     /* alle Topics senden */
} ws[WS_MAX];
static K_MUTEX_DEFINE(ws_mtx);
static uint8_t ws_hs_buf[256];

static bool ws_slot_free(void)
{
	bool free_slot = false;

	k_mutex_lock(&ws_mtx, K_FOREVER);
	for (int i = 0; i < WS_MAX; i++) {
		free_slot |= ws[i].sock < 0;
	}
	k_mutex_unlock(&ws_mtx);
	return free_slot;
}

/*
 * Vorpruefung des WebSocket-Aufbaus, bevor Zephyr "101 Switching Protocols"
 * sendet (Zephyr-Patch patches/zephyr/0001-*). Abgewiesen wird mit einer
 * vollstaendigen HTTP-Antwort wie bei den uebrigen Ressourcen: 401 mit frischer
 * Challenge (evcc erneuert damit eine veraltete Nonce), 429 waehrend der Sperre,
 * 403 im Werkszugang, 503 ohne freien Platz. Danach schliesst der Server.
 */
static struct res ws_res; /* nur fuer gate(): nicht im Werkszugang */
static struct http_header ws_rsp_hdr[3];
static const struct http_header ws_hdr_busy = {.name = "Retry-After", .value = "5"};

static void ws_refuse_headers(struct http_response_ctx *rsp)
{
	size_t n = 0;

	for (size_t i = 0; i < rsp->header_count && n < 1; i++) {
		ws_rsp_hdr[n++] = rsp->headers[i];
	}
	ws_rsp_hdr[n++] = (struct http_header){.name = "Content-Type", .value = "application/json"};
	ws_rsp_hdr[n++] = (struct http_header){.name = "Connection", .value = "close"};
	rsp->headers = ws_rsp_hdr;
	rsp->header_count = n;
}

static int ws_upgrade_check(struct http_client_ctx *client, const struct http_request_ctx *req,
			    struct http_response_ctx *rsp, void *user_data)
{
	ARG_UNUSED(user_data);
	evse_task_http_busy(true); /* SOLL-RT-11 */
	if (!gate(client, &ws_res, req, rsp)) {
		LOG_WRN("WebSocket abgewiesen (%d)", rsp->status);
	} else if (!ws_slot_free()) {
		LOG_WRN("WebSocket: alle %d Plaetze belegt", WS_MAX);
		rsp->headers = &ws_hdr_busy;
		rsp->header_count = 1;
		respond_error(rsp, HTTP_503_SERVICE_UNAVAILABLE, "alle WebSocket-Plätze belegt");
	}
	if (rsp->status != 0) {
		ws_refuse_headers(rsp);
	}
	evse_task_http_busy(false);
	return 0;
}

/* Nach "101": angemeldet ist der Client bereits (ws_upgrade_check) */
static int ws_attach(int sock)
{
	k_mutex_lock(&ws_mtx, K_FOREVER);
	for (int i = 0; i < WS_MAX; i++) {
		if (ws[i].sock < 0) {
			ws[i].sock = sock;
			ws[i].full_sync = true;
			k_mutex_unlock(&ws_mtx);
			k_sem_give(&ws_wake);
			return 0;
		}
	}
	k_mutex_unlock(&ws_mtx);
	LOG_WRN("WebSocket: alle %d Plaetze belegt", WS_MAX);
	return -ENOMEM;
}

static int ws_setup(int sock, struct http_request_ctx *req, void *user_data)
{
	ARG_UNUSED(req);
	ARG_UNUSED(user_data);
	evse_task_http_busy(true); /* SOLL-RT-11 */
	int ret = ws_attach(sock);

	evse_task_http_busy(false);
	return ret;
}

static struct http_resource_detail_websocket ws_detail = {
	.common = {.type = HTTP_RESOURCE_TYPE_WEBSOCKET,
		   .bitmask_of_supported_http_methods = BIT(HTTP_GET)},
	.cb = ws_setup,
	.data_buffer = ws_hs_buf,
	.data_buffer_len = sizeof(ws_hs_buf),
	.upgrade_cb = ws_upgrade_check,
};
HTTP_RESOURCE_DEFINE(ws_r, wallfree_http, "/ws", &ws_detail);

static void ws_close(int i)
{
	(void)websocket_unregister(ws[i].sock);
	ws[i].sock = -1;
}

/*
 * Eingehende Rahmen lesen und verwerfen; Abbau erkennen. Ohne zsock_poll auf
 * dem WebSocket-Deskriptor: websocket_recv_msg prueft den TCP-Socket selbst.
 * Kurzer Timeout statt 0: Die Host-Sockets der PC-Simulation melden
 * Ereignisse erst nach ihrem epoll-Intervall (1 ms); mit 0 blieben dort
 * geschlossene Verbindungen dauerhaft belegt. Auf der Steuerung kostet das
 * hoechstens WS_DRAIN_MS je Client und Zyklus.
 */
static void ws_drain(int i)
{
	uint8_t buf[64];

	for (;;) {
		uint32_t type = 0;
		uint64_t remaining;
		int n = websocket_recv_msg(ws[i].sock, buf, sizeof(buf), &type, &remaining,
					   WS_DRAIN_MS);

		if (n == -EAGAIN) {
			return;
		}
		if (n < 0 || (type & WEBSOCKET_FLAG_CLOSE)) {
			ws_close(i);
			return;
		}
		if (n == 0) {
			return;
		}
	}
}

#define WS_TOPICS 3
static char ws_last[WS_TOPICS][224];
static char ws_cur[WS_TOPICS][224];
static char ws_frame[288];
static const char *const ws_topic[WS_TOPICS] = {"evse/state", "evse/external_current",
						 "evse/limits"};

static void ws_render(void)
{
	struct wf_jbuf b[WS_TOPICS];
	struct evse *e;

	for (int t = 0; t < WS_TOPICS; t++) {
		wf_jb_init(&b[t], ws_cur[t], sizeof(ws_cur[t]));
	}
	e = evse_acquire();
	api_json_state(e, &b[0]);
	api_json_external_current(e, &b[1]);
	api_json_limits(e, &b[2]);
	evse_release();
}

static void ws_send(int i, int t)
{
	int n = snprintf(ws_frame, sizeof(ws_frame), "{\"topic\":\"%s\",\"payload\":%s}",
			 ws_topic[t], ws_cur[t]);

	if (n < 0 || (size_t)n >= sizeof(ws_frame) ||
	    websocket_send_msg(ws[i].sock, (const uint8_t *)ws_frame, (size_t)n,
			       WEBSOCKET_OPCODE_DATA_TEXT, false, true, WS_SEND_MS) < 0) {
		ws_close(i);
	}
}

/*
 * Ereignisse (SOLL-ERR-10): an alle verbundenen Clients als Topic
 * evse/event, in die Logausgabe und ins Log (SOLL-ERR-11). Kein Nachsenden beim Verbinden;
 * verlorene Ereignisse (Ringpuffer uebergelaufen) zeigt die Luecke in seq.
 */
static uint32_t ev_seq = 1;
static char ev_payload[320];
static char ev_frame[384];

static void ws_events(void)
{
	struct wf_event ev;
	uint32_t want = ev_seq;

	while (evse_task_event_get(&ev_seq, &ev)) {
		struct wf_jbuf b;

		if (ev.seq != want) {
			LOG_WRN("%u Ereignisse verloren", ev.seq - want);
		}
		want = ev.seq + 1;
		wf_jb_init(&b, ev_payload, sizeof(ev_payload));
		wf_ev_json(&ev, &b);
		if (b.overflow) {
			continue;
		}
		LOG_INF("Ereignis %s", ev_payload);
		k_mutex_lock(&alog_mtx, K_FOREVER);
		wf_alog_event(&alog, &ev);
		k_mutex_unlock(&alog_mtx);
		int n = snprintf(ev_frame, sizeof(ev_frame), "{\"topic\":\"evse/event\",\"payload\":%s}",
				 ev_payload);

		for (int i = 0; i < WS_MAX; i++) {
			if (ws[i].sock >= 0 && n > 0 && (size_t)n < sizeof(ev_frame) &&
			    websocket_send_msg(ws[i].sock, (const uint8_t *)ev_frame, (size_t)n,
					       WEBSOCKET_OPCODE_DATA_TEXT, false, true, WS_SEND_MS) < 0) {
				ws_close(i);
			}
		}
	}
}

/*
 * Zaehler-Topics (SOLL-MTR-30): nach jedem Zyklus Werte und Zustand an alle,
 * beim Verbinden zusaetzlich die Kennungen. Kein Vergleich mit dem letzten
 * Stand, daher ohne eigene Puffer je Topic.
 */
static uint32_t meter_cycles;
static char meter_frame[448];

static void ws_meter(void)
{
	struct meter_values mv;
	uint32_t n = meter_task_get(&mv);
	bool cycle = n != meter_cycles;

	meter_cycles = n;
	for (int t = 0; t < MT_COUNT; t++) {
		struct wf_jbuf b;
		bool any = false;

		for (int i = 0; i < WS_MAX; i++) {
			any |= ws[i].sock >= 0 && (ws[i].full_sync || (cycle && t != MT_VALUE_IDS));
		}
		if (!any) {
			continue;
		}
		wf_jb_init(&b, meter_frame, sizeof(meter_frame));
		wf_jb_raw(&b, "{\"topic\":");
		wf_jb_str(&b, meter_topic[t]);
		wf_jb_raw(&b, ",\"payload\":");
		meter_render((enum meter_topic)t, &b);
		wf_jb_raw(&b, "}");
		if (b.overflow) {
			continue;
		}
		for (int i = 0; i < WS_MAX; i++) {
			if (ws[i].sock >= 0 && (ws[i].full_sync || (cycle && t != MT_VALUE_IDS)) &&
			    websocket_send_msg(ws[i].sock, (const uint8_t *)meter_frame, b.len,
					       WEBSOCKET_OPCODE_DATA_TEXT, false, true, WS_SEND_MS) < 0) {
				ws_close(i);
			}
		}
	}
}

static void ws_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int64_t beat = 0;

	for (;;) {
		(void)k_sem_take(&ws_wake, K_MSEC(WS_PERIOD_MS));
		evse_task_iface_beat(EVSE_IFACE_WS);
		ws_render();

		k_mutex_lock(&ws_mtx, K_FOREVER);
		for (int i = 0; i < WS_MAX; i++) {
			if (ws[i].sock >= 0) {
				ws_drain(i);
			}
		}
		bool beat_due = k_uptime_get() - beat >= WS_BEAT_MS;

		if (beat_due) {
			beat = k_uptime_get();
		}
		for (int t = 0; t < WS_TOPICS; t++) {
			bool changed = strcmp(ws_cur[t], ws_last[t]) != 0 || (t == 0 && beat_due);

			for (int i = 0; i < WS_MAX; i++) {
				if (ws[i].sock >= 0 && (changed || ws[i].full_sync)) {
					ws_send(i, t);
				}
			}
			strcpy(ws_last[t], ws_cur[t]);
		}
		if (meter_task_type() != METER_NONE) {
			ws_meter();
		}
		for (int i = 0; i < WS_MAX; i++) {
			ws[i].full_sync = false;
		}
		ws_events();
		k_mutex_unlock(&ws_mtx);
	}
}

K_THREAD_STACK_DEFINE(ws_stack, WS_STACK);
static struct k_thread ws_thread;

void http_api_set_wall_base(int64_t wall_base)
{
	k_mutex_lock(&alog_mtx, K_FOREVER);
	wf_alog_set_wall_base(&alog, wall_base);
	k_mutex_unlock(&alog_mtx);
}

int http_api_start(void)
{
	for (int i = 0; i < WS_MAX; i++) {
		ws[i].sock = -1;
	}
	wf_rl_init(&rl);
	hw_retained_attach(&alog, sizeof(alog));
	if (wf_alog_restore(&alog)) {
		LOG_INF("Protokoll aus dem vorigen Lauf uebernommen (Start %u)", alog.boot);
	}
	auth_init();
	k_thread_create(&ws_thread, ws_stack, K_THREAD_STACK_SIZEOF(ws_stack), ws_entry, NULL,
			NULL, NULL, WS_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&ws_thread, "ws_push");
#if defined(CONFIG_ARCH_POSIX)
	http_port = hw_sim_http_port();
#endif
	int r = http_server_start();

	if (r != 0) {
		LOG_ERR("HTTP-Server: %d", r);
	} else {
		LOG_INF("HTTP-Server auf Port %u", http_port);
	}
	return r;
}
