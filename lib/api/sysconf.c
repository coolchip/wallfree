/* SPDX-License-Identifier: Apache-2.0 */
/* Netz- und Systemparameter, siehe include/wallfree/api/sysconf.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/sysconf.h>

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))

static const struct wf_syspar pars[] = {
	{"net_dhcp", 3010, WF_SYS_BOOL, "Adresse per DHCP beziehen"},
	{"net_ip", 3011, WF_SYS_IPV4, "feste IPv4-Adresse (ohne DHCP)"},
	{"net_netmask", 3012, WF_SYS_IPV4, "Netzmaske (ohne DHCP)"},
	{"net_gateway", 3013, WF_SYS_IPV4, "Gateway (ohne DHCP), 0.0.0.0 = keins"},
	{"net_hostname", 3014, WF_SYS_HOST, "Hostname, leer = wallfree-<MAC-Ende>"},
	{"telnet_enable", 3015, WF_SYS_BOOL, "Telnet-Shell einschalten (Zugang ohne Passwort)"},
	{"meter_type", 3016, WF_SYS_CODE,
	 "Zählertyp: 0 keiner, 1 ABB B23, 2 Eastron SDM630, 3 Eastron "
	 "SDM72D-M V2, 4 Eastron SDM72D-M (V1, nur Summenwerte)"},
	{"meter_address", 3017, WF_SYS_CODE, "Modbus-Adresse des Zählers 1..247"},
	{"meter_baud", 3018, WF_SYS_CODE, "Baudrate RS-485: 2400..115200"},
	{"meter_framing", 3019, WF_SYS_CODE, "Rahmenformat: 0 = 8N1, 1 = 8E1, 2 = 8O1, 3 = 8N2"},
	{"mqtt_enable", 3020, WF_SYS_BOOL, "MQTT-Client, wirkt sofort"},
	{"mqtt_broker", 3021, WF_SYS_IPV4, "IPv4-Adresse des Brokers"},
	{"mqtt_port", 3022, WF_SYS_CODE, "Port des Brokers 1..65535"},
	{"mqtt_user", 3023, WF_SYS_TEXT, "Benutzer am Broker, leer = ohne Anmeldung"},
	{"mqtt_password", 3024, WF_SYS_SECRET, "Passwort am Broker (nicht lesbar)"},
	{"mqtt_prefix", 3025, WF_SYS_TEXT, "Topic-Präfix, leer = wallfree/<MAC-Ende>"},
	{"mqtt_interval", 3026, WF_SYS_CODE, "Mindestabstand je Topic in s, 0..3600"},
	{"mqtt_commands", 3027, WF_SYS_BOOL, "Befehle annehmen (Ladestrom setzen)"},
};
#define N_PARS (sizeof(pars) / sizeof(pars[0]))

void wf_sysconf_defaults(struct wf_sysconf *c)
{
	memset(c, 0, sizeof(*c));
	c->dhcp = true;                     /* F25 */
	c->ip = IP(192, 168, 0, 8);         /* wie Original */
	c->netmask = IP(255, 255, 255, 0);
	c->gateway = IP(192, 168, 0, 1);
	c->telnet = false;                  /* F20: ab Werk aus (Nutzer 2026-10-26) */
	c->meter_addr = 1;                  /* Werkseinstellung der Eastron-Zaehler */
	c->meter_baud = 9600;
	c->meter_framing = WF_FRAMING_8N1;
	c->mqtt_port = 1883;
	c->mqtt_interval = 1;
}

size_t wf_sys_count(void)
{
	return N_PARS;
}

const struct wf_syspar *wf_sys_at(size_t i)
{
	return i < N_PARS ? &pars[i] : NULL;
}

const struct wf_syspar *wf_sys_find(const char *name)
{
	for (size_t i = 0; i < N_PARS; i++) {
		if (strcmp(pars[i].name, name) == 0) {
			return &pars[i];
		}
	}
	return NULL;
}

const struct wf_syspar *wf_sys_by_id(uint16_t id)
{
	for (size_t i = 0; i < N_PARS; i++) {
		if (pars[i].id == id) {
			return &pars[i];
		}
	}
	return NULL;
}

/* ------------------------------------------------ IPv4 */

int wf_ipv4_parse(const char *s, uint32_t *out)
{
	uint32_t ip = 0;

	for (int part = 0; part < 4; part++) {
		int v = 0, digits = 0;

		if (part > 0) {
			if (*s++ != '.') {
				return -1;
			}
		}
		while (*s >= '0' && *s <= '9') {
			if (digits > 0 && v == 0) {
				return -1; /* fuehrende Null (mehrdeutig, oktal) */
			}
			v = v * 10 + (*s++ - '0');
			if (++digits > 3 || v > 255) {
				return -1;
			}
		}
		if (digits == 0) {
			return -1;
		}
		ip = (ip << 8) | (uint32_t)v;
	}
	if (*s != '\0') {
		return -1;
	}
	*out = ip;
	return 0;
}

void wf_ipv4_format(uint32_t ip, char out[16])
{
	snprintf(out, 16, "%u.%u.%u.%u", (unsigned int)(ip >> 24), (unsigned int)(ip >> 16) & 255,
		 (unsigned int)(ip >> 8) & 255, (unsigned int)ip & 255);
}

static bool unicast(uint32_t ip)
{
	uint32_t a = ip >> 24;

	return a >= 1 && a <= 223 && a != 127;
}

static int mask_prefix(uint32_t m)
{
	int n = 0;

	while (n < 32 && (m & (0x80000000u >> n))) {
		n++;
	}
	/* zusammenhaengend? */
	return (n == 32 || (m << n) == 0) ? n : -1;
}

static const char *check_ip(const struct wf_syspar *p, uint32_t v)
{
	if (p->id == 3011 && !unicast(v)) {
		return "keine gültige Einzeladresse";
	}
	if (p->id == 3012) {
		int n = mask_prefix(v);

		if (n < 8 || n > 30) {
			return "Netzmaske: zusammenhängend, /8 bis /30";
		}
	}
	if (p->id == 3013 && v != 0 && !unicast(v)) {
		return "keine gültige Gateway-Adresse";
	}
	/* Broker: auch 127.x (PC-Simulation mit Broker auf demselben Rechner) */
	if (p->id == 3021 && v != 0 && !unicast(v) && (v >> 24) != 127) {
		return "keine gültige Broker-Adresse";
	}
	return NULL;
}

static const char *check_host(const char *h, size_t n)
{
	if (n == 0) {
		return NULL; /* automatisch */
	}
	if (n > WF_HOSTNAME_MAX || h[0] == '-' || h[n - 1] == '-') {
		return "Hostname: 1..32 Zeichen, nicht mit '-' beginnend oder endend";
	}
	for (size_t i = 0; i < n; i++) {
		char ch = h[i];

		if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
		      (ch >= '0' && ch <= '9') || ch == '-')) {
			return "Hostname: nur Buchstaben, Ziffern und '-'";
		}
	}
	return NULL;
}

/* SOLL-MQTT-12: keine Platzhalter, kein $-Topic, keine leere Ebene am Rand */
static const char *check_prefix(const char *h, size_t n)
{
	if (n > 0 && (h[0] == '/' || h[0] == '$' || h[n - 1] == '/')) {
		return "Präfix: nicht mit '/' oder '$' beginnend, nicht mit '/' endend";
	}
	for (size_t i = 0; i < n; i++) {
		if (h[i] == '+' || h[i] == '#') {
			return "Präfix: ohne '+' und '#'";
		}
		if (h[i] == ' ') {
			return "Präfix: ohne Leerzeichen";
		}
	}
	return NULL;
}

static char *text_field(struct wf_sysconf *c, uint16_t id, size_t *max)
{
	switch (id) {
	case 3023:
		*max = WF_MQTT_USER_MAX;
		return c->mqtt_user;
	case 3024:
		*max = WF_MQTT_PW_MAX;
		return c->mqtt_password;
	case 3025:
		*max = WF_MQTT_PREFIX_MAX;
		return c->mqtt_prefix;
	default:
		*max = WF_HOSTNAME_MAX;
		return c->hostname;
	}
}

static const char *check_text(uint16_t id, const char *h, size_t n)
{
	if (id == 3014) {
		return check_host(h, n);
	}
	if (n > (id == 3023 ? WF_MQTT_USER_MAX : WF_MQTT_PW_MAX)) {
		return id == 3023 ? "höchstens 32 Zeichen" : "höchstens 64 Zeichen";
	}
	for (size_t i = 0; i < n; i++) {
		if (h[i] < 0x20 || h[i] > 0x7E) {
			return "nur druckbare ASCII-Zeichen";
		}
	}
	return id == 3025 ? check_prefix(h, n) : NULL;
}

static const uint32_t bauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};

static const char *check_code(uint16_t id, int64_t v)
{
	switch (id) {
	case 3016:
		return v >= 0 && v <= 4 ? NULL : "Zählertyp 0..4";
	case 3017:
		return v >= 1 && v <= 247 ? NULL : "Adresse 1..247";
	case 3018:
		for (size_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
			if (v == bauds[i]) {
				return NULL;
			}
		}
		return "Baudrate 2400, 4800, 9600, 19200, 38400, 57600 oder 115200";
	case 3022:
		return v >= 1 && v <= 65535 ? NULL : "Port 1..65535";
	case 3026:
		return v >= 0 && v <= 3600 ? NULL : "Intervall 0..3600 s";
	default:
		return v >= 0 && v <= 3 ? NULL : "Rahmenformat 0..3";
	}
}

static uint32_t code_get(const struct wf_sysconf *c, uint16_t id)
{
	switch (id) {
	case 3016:
		return c->meter_type;
	case 3017:
		return c->meter_addr;
	case 3018:
		return c->meter_baud;
	case 3022:
		return c->mqtt_port;
	case 3026:
		return c->mqtt_interval;
	default:
		return c->meter_framing;
	}
}

static void code_put(struct wf_sysconf *c, uint16_t id, uint32_t v)
{
	switch (id) {
	case 3016:
		c->meter_type = (uint8_t)v;
		break;
	case 3017:
		c->meter_addr = (uint8_t)v;
		break;
	case 3018:
		c->meter_baud = v;
		break;
	case 3022:
		c->mqtt_port = (uint16_t)v;
		break;
	case 3026:
		c->mqtt_interval = (uint16_t)v;
		break;
	default:
		c->meter_framing = (uint8_t)v;
		break;
	}
}

static uint32_t *ip_field(struct wf_sysconf *c, uint16_t id)
{
	return id == 3011 ? &c->ip : id == 3012 ? &c->netmask
		: id == 3013 ? &c->gateway : &c->mqtt_broker;
}

static bool *bool_field(struct wf_sysconf *c, uint16_t id)
{
	return id == 3010 ? &c->dhcp : id == 3015 ? &c->telnet
		: id == 3020 ? &c->mqtt_enable : &c->mqtt_commands;
}

const char *wf_sys_set(struct wf_sysconf *c, const struct wf_syspar *p, const struct wf_jval *v)
{
	switch (p->type) {
	case WF_SYS_BOOL:
		if ((v->type != WF_J_BOOL && v->type != WF_J_NUM) || v->num < 0 || v->num > 1) {
			return "true/false oder 0/1 erwartet";
		}
		*bool_field(c, p->id) = v->num != 0;
		return NULL;
	case WF_SYS_IPV4: {
		uint32_t ip;

		if (v->type != WF_J_STR || wf_ipv4_parse(v->str, &ip) != 0) {
			return "IPv4-Adresse als Text erwartet (a.b.c.d)";
		}
		const char *err = check_ip(p, ip);

		if (err == NULL) {
			*ip_field(c, p->id) = ip;
		}
		return err;
	}
	case WF_SYS_CODE: {
		if (v->type != WF_J_NUM) {
			return "Zahl erwartet";
		}
		const char *err = check_code(p->id, v->num);

		if (err == NULL) {
			code_put(c, p->id, (uint32_t)v->num);
		}
		return err;
	}
	default: {
		size_t max;
		char *dst = text_field(c, p->id, &max);

		if (v->type != WF_J_STR) {
			return "Text erwartet";
		}
		const char *err = check_text(p->id, v->str, v->str_len);

		if (err == NULL) {
			memcpy(dst, v->str, v->str_len + 1);
		}
		return err;
	}
	}
}

const char *wf_sys_check(const struct wf_sysconf *c)
{
	uint32_t m = c->netmask;

	if ((c->ip & ~m) == 0 || (c->ip & ~m) == ~m) {
		return "Adresse ist Netz- oder Broadcastadresse";
	}
	if (c->gateway != 0 && ((c->gateway & m) != (c->ip & m) || c->gateway == c->ip)) {
		return "Gateway liegt nicht im Netz der Adresse";
	}
	if (c->mqtt_enable && c->mqtt_broker == 0) {
		return "MQTT: Broker-Adresse fehlt (mqtt_broker)";
	}
	return NULL;
}

const char *wf_sys_repair(struct wf_sysconf *c)
{
	const char *what = NULL;

	if (c->mqtt_enable && c->mqtt_broker == 0) {
		c->mqtt_enable = false;
		what = "MQTT ohne Broker, aus";
	}
	if (wf_sys_check(c) != NULL) {
		/* widerspruechliche feste Adresse: lieber DHCP als gar kein Netz */
		c->dhcp = true;
		what = "feste Adresse ungueltig, DHCP";
	}
	return what;
}

void wf_sys_take_live(struct wf_sysconf *dst, const struct wf_sysconf *src)
{
	dst->mqtt_enable = src->mqtt_enable;
	dst->mqtt_commands = src->mqtt_commands;
	dst->mqtt_port = src->mqtt_port;
	dst->mqtt_interval = src->mqtt_interval;
	dst->mqtt_broker = src->mqtt_broker;
	memcpy(dst->mqtt_user, src->mqtt_user, sizeof(dst->mqtt_user));
	memcpy(dst->mqtt_password, src->mqtt_password, sizeof(dst->mqtt_password));
	memcpy(dst->mqtt_prefix, src->mqtt_prefix, sizeof(dst->mqtt_prefix));
}

void wf_sys_json(const struct wf_sysconf *c, struct wf_jbuf *b)
{
	char ip[16];

	wf_jb_raw(b, "{");
	for (size_t i = 0; i < N_PARS; i++) {
		const struct wf_syspar *p = &pars[i];

		wf_jb_key(b, p->name, i == 0);
		switch (p->type) {
		case WF_SYS_BOOL:
			wf_jb_raw(b, *bool_field((struct wf_sysconf *)c, p->id) ? "true" : "false");
			break;
		case WF_SYS_IPV4:
			wf_ipv4_format(*ip_field((struct wf_sysconf *)c, p->id), ip);
			wf_jb_str(b, ip);
			break;
		case WF_SYS_CODE:
			wf_jb_int(b, code_get(c, p->id));
			break;
		case WF_SYS_SECRET:
			wf_jb_raw(b, "null"); /* SOLL-MQTT-02: nicht lesbar */
			break;
		default: {
			size_t max;

			wf_jb_str(b, text_field((struct wf_sysconf *)c, p->id, &max));
			break;
		}
		}
	}
	wf_jb_raw(b, "}");
}

/* Ausgabe ab @p mark verwerfen (passt nicht mehr, kommt im naechsten Teil) */
static bool undo_overflow(struct wf_jbuf *b, size_t mark)
{
	if (!b->overflow) {
		return false;
	}
	b->len = mark;
	b->p[mark] = '\0';
	b->overflow = false;
	return true;
}

size_t wf_sys_info_json(size_t idx, struct wf_jbuf *b)
{
	static const char *const types[] = {"bool", "ipv4", "hostname", "code", "text", "secret"};

	if (idx == 0) {
		wf_jb_raw(b, "[");
	}
	for (; idx < N_PARS; idx++) {
		size_t mark = b->len;

		wf_jb_raw(b, idx == 0 ? "{" : ",{");
		wf_jb_key(b, "name", true);
		wf_jb_str(b, pars[idx].name);
		wf_jb_key(b, "type", false);
		wf_jb_str(b, types[pars[idx].type]);
		wf_jb_key(b, "help", false);
		wf_jb_str(b, pars[idx].help);
		wf_jb_raw(b, "}");
		if (undo_overflow(b, mark)) {
			return idx;
		}
	}
	size_t mark = b->len;

	wf_jb_raw(b, "]");
	return undo_overflow(b, mark) ? N_PARS : N_PARS + 1;
}

size_t wf_sys_encode(const struct wf_sysconf *c, const struct wf_syspar *p, uint8_t *buf)
{
	switch (p->type) {
	case WF_SYS_BOOL:
		buf[0] = *bool_field((struct wf_sysconf *)c, p->id) ? 1 : 0;
		return 1;
	case WF_SYS_IPV4: {
		uint32_t v = *ip_field((struct wf_sysconf *)c, p->id);

		buf[0] = (uint8_t)(v >> 24);
		buf[1] = (uint8_t)(v >> 16);
		buf[2] = (uint8_t)(v >> 8);
		buf[3] = (uint8_t)v;
		return 4;
	}
	case WF_SYS_CODE: {
		uint32_t v = code_get(c, p->id);

		buf[0] = (uint8_t)(v >> 24);
		buf[1] = (uint8_t)(v >> 16);
		buf[2] = (uint8_t)(v >> 8);
		buf[3] = (uint8_t)v;
		return 4;
	}
	default: {
		size_t max;
		const char *t = text_field((struct wf_sysconf *)c, p->id, &max);
		size_t n = strlen(t);

		if (n == 0) {
			buf[0] = 0; /* leer: ein NUL, weil der Speicher keine leeren Werte kennt */
			return 1;
		}
		memcpy(buf, t, n);
		return n;
	}
	}
}

int wf_sys_decode(struct wf_sysconf *c, uint16_t id, const void *data, size_t len)
{
	const struct wf_syspar *p = wf_sys_by_id(id);
	const uint8_t *d = data;

	if (p == NULL) {
		return -1;
	}
	switch (p->type) {
	case WF_SYS_BOOL:
		if (len != 1 || d[0] > 1) {
			return -1;
		}
		*bool_field(c, id) = d[0] != 0;
		return 0;
	case WF_SYS_IPV4: {
		if (len != 4) {
			return -1;
		}
		uint32_t v = IP(d[0], d[1], d[2], d[3]);

		if (check_ip(p, v) != NULL) {
			return -1;
		}
		*ip_field(c, id) = v;
		return 0;
	}
	case WF_SYS_CODE: {
		if (len != 4) {
			return -1;
		}
		uint32_t v = IP(d[0], d[1], d[2], d[3]);

		if (check_code(id, v) != NULL) {
			return -1;
		}
		code_put(c, id, v);
		return 0;
	}
	default: {
		size_t max;
		char *t = text_field(c, id, &max);

		if (len == 1 && d[0] == 0) {
			t[0] = '\0';
			return 0;
		}
		if (len == 0 || check_text(id, data, len) != NULL) {
			return -1;
		}
		memcpy(t, data, len);
		t[len] = '\0';
		return 0;
	}
	}
}

static int orig_ip(wf_orig_lookup_t lookup, void *ctx, uint16_t first, uint32_t *out)
{
	uint32_t ip = 0;

	for (uint16_t f = first; f < first + 4; f++) {
		uint16_t v;

		if (lookup(ctx, f, &v) != 0) {
			return 1; /* nicht gespeichert */
		}
		if (v > 255) {
			return -1;
		}
		ip = (ip << 8) | v;
	}
	*out = ip;
	return 0;
}

int wf_sys_import_orig(struct wf_sysconf *c, wf_orig_lookup_t lookup, void *ctx,
		       const char **err)
{
	struct wf_sysconf t = *c;
	int n = 0;
	uint16_t v;
	static const struct {
		uint16_t field;
		uint16_t id;
	} ips[] = {{315, 3011}, {319, 3012}, {323, 3013}};

	*err = NULL;
	if (lookup(ctx, 404, &v) == 0) {
		/* 1-Byte-Feld im unteren Byte des Halbworts */
		if ((v & 0xFF) > 1) {
			*err = "DHCP-Feld 404 ungueltig";
			return -1;
		}
		t.dhcp = (v & 0xFF) != 0;
		n++;
	}
	for (size_t i = 0; i < sizeof(ips) / sizeof(ips[0]); i++) {
		uint32_t ip;
		int r = orig_ip(lookup, ctx, ips[i].field, &ip);

		if (r < 0 || (r == 0 && check_ip(wf_sys_by_id(ips[i].id), ip) != NULL)) {
			*err = "Adressfelder 315..326 ungueltig";
			return -1;
		}
		if (r == 0) {
			*ip_field(&t, ips[i].id) = ip;
			n++;
		}
	}
	*err = wf_sys_check(&t);
	if (*err != NULL) {
		return -1;
	}
	*c = t;
	return n;
}
