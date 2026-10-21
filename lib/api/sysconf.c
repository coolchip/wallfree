/* SPDX-License-Identifier: Apache-2.0 */
/* Netz- und Systemparameter, siehe include/wallfree/api/sysconf.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/sysconf.h>

#define IP(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (d))

static const struct wf_syspar pars[] = {
	{"net_dhcp", 3010, WF_SYS_BOOL, "Adresse per DHCP beziehen (F25)"},
	{"net_ip", 3011, WF_SYS_IPV4, "feste IPv4-Adresse (ohne DHCP)"},
	{"net_netmask", 3012, WF_SYS_IPV4, "Netzmaske (ohne DHCP)"},
	{"net_gateway", 3013, WF_SYS_IPV4, "Gateway (ohne DHCP), 0.0.0.0 = keins"},
	{"net_hostname", 3014, WF_SYS_HOST, "Hostname, leer = wallfree-<MAC-Ende>"},
	{"telnet_enable", 3015, WF_SYS_BOOL, "Telnet-Shell ohne Anmeldung (F20)"},
	{"meter_type", 3016, WF_SYS_CODE,
	 "Energiezaehler (nur Anzeige): 0 keiner, 1 ABB B23, 2 Eastron SDM630, 3 Eastron "
	 "SDM72D-M V2, 4 Eastron SDM72D-M (V1, nur Summenwerte)"},
	{"meter_address", 3017, WF_SYS_CODE, "Modbus-Adresse des Zaehlers 1..247"},
	{"meter_baud", 3018, WF_SYS_CODE, "Baudrate RS-485: 2400..115200"},
	{"meter_framing", 3019, WF_SYS_CODE, "Rahmenformat: 0 = 8N1, 1 = 8E1, 2 = 8O1, 3 = 8N2"},
};
#define N_PARS (sizeof(pars) / sizeof(pars[0]))

void wf_sysconf_defaults(struct wf_sysconf *c)
{
	memset(c, 0, sizeof(*c));
	c->dhcp = true;                     /* F25 */
	c->ip = IP(192, 168, 0, 8);         /* wie Original */
	c->netmask = IP(255, 255, 255, 0);
	c->gateway = IP(192, 168, 0, 1);
	c->telnet = true;                   /* F20, spaeter Default aus */
	c->meter_addr = 1;                  /* Werkseinstellung der Eastron-Zaehler */
	c->meter_baud = 9600;
	c->meter_framing = WF_FRAMING_8N1;
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
		return "keine gueltige Einzeladresse";
	}
	if (p->id == 3012) {
		int n = mask_prefix(v);

		if (n < 8 || n > 30) {
			return "Netzmaske: zusammenhaengend, /8 bis /30";
		}
	}
	if (p->id == 3013 && v != 0 && !unicast(v)) {
		return "keine gueltige Gateway-Adresse";
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

static const uint32_t bauds[] = {2400, 4800, 9600, 19200, 38400, 57600, 115200};

static const char *check_code(uint16_t id, int64_t v)
{
	switch (id) {
	case 3016:
		return v >= 0 && v <= 4 ? NULL : "Zaehlertyp 0..4";
	case 3017:
		return v >= 1 && v <= 247 ? NULL : "Adresse 1..247";
	case 3018:
		for (size_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
			if (v == bauds[i]) {
				return NULL;
			}
		}
		return "Baudrate 2400, 4800, 9600, 19200, 38400, 57600 oder 115200";
	default:
		return v >= 0 && v <= 3 ? NULL : "Rahmenformat 0..3";
	}
}

static uint32_t code_get(const struct wf_sysconf *c, uint16_t id)
{
	return id == 3016 ? c->meter_type : id == 3017 ? c->meter_addr
		: id == 3018 ? c->meter_baud : c->meter_framing;
}

static void code_put(struct wf_sysconf *c, uint16_t id, uint32_t v)
{
	if (id == 3016) {
		c->meter_type = (uint8_t)v;
	} else if (id == 3017) {
		c->meter_addr = (uint8_t)v;
	} else if (id == 3018) {
		c->meter_baud = v;
	} else {
		c->meter_framing = (uint8_t)v;
	}
}

static uint32_t *ip_field(struct wf_sysconf *c, uint16_t id)
{
	return id == 3011 ? &c->ip : id == 3012 ? &c->netmask : &c->gateway;
}

static bool *bool_field(struct wf_sysconf *c, uint16_t id)
{
	return id == 3010 ? &c->dhcp : &c->telnet;
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
		if (v->type != WF_J_STR) {
			return "Text erwartet";
		}
		const char *err = check_host(v->str, v->str_len);

		if (err == NULL) {
			memcpy(c->hostname, v->str, v->str_len + 1);
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
	return NULL;
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
		default:
			wf_jb_str(b, c->hostname);
			break;
		}
	}
	wf_jb_raw(b, "}");
}

void wf_sys_info_json(struct wf_jbuf *b)
{
	static const char *const types[] = {"bool", "ipv4", "hostname", "code"};

	wf_jb_raw(b, "[");
	for (size_t i = 0; i < N_PARS; i++) {
		wf_jb_raw(b, i == 0 ? "{" : ",{");
		wf_jb_key(b, "name", true);
		wf_jb_str(b, pars[i].name);
		wf_jb_key(b, "type", false);
		wf_jb_str(b, types[pars[i].type]);
		wf_jb_key(b, "help", false);
		wf_jb_str(b, pars[i].help);
		wf_jb_raw(b, "}");
	}
	wf_jb_raw(b, "]");
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
		size_t n = strlen(c->hostname);

		if (n == 0) {
			buf[0] = 0; /* leer: ein NUL, weil der Speicher keine leeren Werte kennt */
			return 1;
		}
		memcpy(buf, c->hostname, n);
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
	default:
		if (len == 1 && d[0] == 0) {
			c->hostname[0] = '\0';
			return 0;
		}
		if (check_host(data, len) != NULL || len == 0) {
			return -1;
		}
		memcpy(c->hostname, data, len);
		c->hostname[len] = '\0';
		return 0;
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
