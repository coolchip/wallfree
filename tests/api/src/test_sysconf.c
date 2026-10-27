/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Netz- und Systemparameter (10.1 net_*, telnet_enable, meter_*, mqtt_*; F20, F25, F28, F30;
 * SOLL-NVM-10) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/sysconf.h>

static struct wf_sysconf c;

static void before(void *f)
{
	ARG_UNUSED(f);
	wf_sysconf_defaults(&c);
}

ZTEST_SUITE(api_sysconf, NULL, NULL, before, NULL, NULL);

static struct wf_jval S(const char *s)
{
	return (struct wf_jval){.type = WF_J_STR, .str = s, .str_len = strlen(s)};
}

static struct wf_jval N(int64_t n)
{
	return (struct wf_jval){.type = WF_J_NUM, .num = n};
}

static const char *set(const char *name, struct wf_jval v)
{
	const struct wf_syspar *p = wf_sys_find(name);

	zassert_not_null(p, "%s", name);
	return wf_sys_set(&c, p, &v);
}

ZTEST(api_sysconf, test_defaults_and_json)
{
	char buf[512];
	struct wf_jbuf b;

	wf_jb_init(&b, buf, sizeof(buf));
	wf_sys_json(&c, &b);
	zassert_str_equal(buf, "{\"net_dhcp\":true,\"net_ip\":\"192.168.0.8\","
			       "\"net_netmask\":\"255.255.255.0\",\"net_gateway\":\"192.168.0.1\","
			       "\"net_hostname\":\"\",\"telnet_enable\":false,\"meter_type\":0,"
			       "\"meter_address\":1,\"meter_baud\":9600,\"meter_framing\":0,"
			       "\"mqtt_enable\":false,\"mqtt_broker\":\"\",\"mqtt_port\":1883,"
			       "\"mqtt_user\":\"\",\"mqtt_password\":null,\"mqtt_prefix\":\"\","
			       "\"mqtt_interval\":1,\"mqtt_commands\":false,\"mqtt_discovery\":true,"
			       "\"ntp_enable\":true,"
			       "\"ntp_server\":\"\"}");
	zassert_is_null(wf_sys_check(&c));
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);

		zassert_true(p->id >= WF_SYS_ID_FIRST);
		zassert_equal(wf_sys_by_id(p->id), p);
	}
	zassert_is_null(wf_sys_at(wf_sys_count()));
	zassert_is_null(wf_sys_find("nix"));
	zassert_is_null(wf_sys_by_id(1));

	char info[2560];

	wf_jb_init(&b, info, sizeof(info));
	zassert_equal(wf_sys_info_json(0, &b), wf_sys_count() + 1, "passt in einem Teil");
	zassert_false(b.overflow);
	zassert_not_null(strstr(info, "{\"name\":\"net_ip\",\"type\":\"ipv4\""));
	zassert_not_null(strstr(info, "{\"name\":\"meter_type\",\"type\":\"code\""));
	zassert_not_null(strstr(info, "{\"name\":\"mqtt_password\",\"type\":\"secret\""));
	zassert_not_null(strstr(info, "{\"name\":\"mqtt_prefix\",\"type\":\"text\""));
}

/* Beschreibung in Teilen (kleiner Puffer wie die HTTP-Antwort) ergibt dieselbe Liste */
ZTEST(api_sysconf, test_info_parts)
{
	static char whole[2560], joined[2560], part[200];
	struct wf_jbuf b;
	size_t idx = 0, parts = 0;

	wf_jb_init(&b, whole, sizeof(whole));
	(void)wf_sys_info_json(0, &b);
	joined[0] = '\0';
	while (idx <= wf_sys_count()) {
		wf_jb_init(&b, part, sizeof(part));
		size_t next = wf_sys_info_json(idx, &b);

		zassert_true(next > idx || b.len > 0, "Fortschritt");
		zassert_false(b.overflow);
		strcat(joined, part);
		idx = next;
		parts++;
	}
	zassert_true(parts > 3);
	zassert_str_equal(joined, whole);
}

/* 10.1 mqtt_*, SOLL-MQTT-01/02/12 */
ZTEST(api_sysconf, test_mqtt_params)
{
	zassert_false(c.mqtt_enable, "Werkszustand aus");
	zassert_false(c.mqtt_commands, "Werkszustand ohne Befehle");
	zassert_is_null(set("mqtt_broker", S("192.168.1.50")));
	zassert_str_equal(c.mqtt_broker, "192.168.1.50");
	zassert_is_null(set("mqtt_broker", S("")), "keiner");
	zassert_not_null(set("mqtt_broker", S("0.0.0.0")));
	zassert_not_null(set("mqtt_broker", S("224.0.0.1")));
	zassert_is_null(set("mqtt_broker", S("127.0.0.1")), "Simulation");
	/* SOLL-MQTT-02: auch ein Name (DNS), ohne Port */
	zassert_is_null(set("mqtt_broker", S("homeassistant.local")));
	zassert_str_equal(c.mqtt_broker, "homeassistant.local");
	zassert_not_null(set("mqtt_broker", S("broker:1883")), "Port ist mqtt_port");
	zassert_not_null(set("mqtt_broker", S("a b")));
	zassert_not_null(set("mqtt_broker", S("192.168.1.300")));

	zassert_is_null(set("mqtt_port", N(1)));
	zassert_is_null(set("mqtt_port", N(65535)));
	zassert_equal(c.mqtt_port, 65535);
	zassert_not_null(set("mqtt_port", N(0)));
	zassert_not_null(set("mqtt_port", N(65536)));
	zassert_is_null(set("mqtt_interval", N(0)));
	zassert_is_null(set("mqtt_interval", N(3600)));
	zassert_not_null(set("mqtt_interval", N(3601)));
	zassert_not_null(set("mqtt_interval", N(-1)));

	zassert_is_null(set("mqtt_user", S("")));
	zassert_is_null(set("mqtt_user", S("12345678901234567890123456789012")));
	zassert_not_null(set("mqtt_user", S("123456789012345678901234567890123")));
	zassert_not_null(set("mqtt_user", S("a\tb")), "Steuerzeichen");
	zassert_is_null(set("mqtt_password", S("geheim mit Leerzeichen!")));
	zassert_str_equal(c.mqtt_password, "geheim mit Leerzeichen!");
	zassert_not_null(set("mqtt_password", S("\xc3\xa4")), "kein ASCII");

	zassert_is_null(set("mqtt_prefix", S("garage/wallbox-1")));
	zassert_str_equal(c.mqtt_prefix, "garage/wallbox-1");
	zassert_is_null(set("mqtt_prefix", S("")), "automatisch");
	static const char *const bad[] = {"/a", "a/", "$SYS/x", "a/+/b", "a/#", "a b", "a\x01"};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_not_null(set("mqtt_prefix", S(bad[i])), "%s", bad[i]);
	}
	zassert_is_null(set("mqtt_prefix",
			    S("1234567890123456789012345678901234567890123456789012345678901234")));
	zassert_not_null(set("mqtt_prefix",
			     S("12345678901234567890123456789012345678901234567890123456789012345")));

	/* einschalten nur mit Broker */
	zassert_is_null(set("mqtt_broker", S("")));
	zassert_is_null(set("mqtt_enable", N(1)));
	zassert_not_null(wf_sys_check(&c));
	zassert_is_null(set("mqtt_broker", S("192.168.1.50")));
	zassert_is_null(wf_sys_check(&c));
}

/* SOLL-MQTT-31: sofort wirksame Werte; Start mit widerspruechlichem Satz */
ZTEST(api_sysconf, test_live_and_repair)
{
	struct wf_sysconf a;

	wf_sysconf_defaults(&a);
	zassert_is_null(set("mqtt_enable", N(1)));
	zassert_is_null(set("mqtt_broker", S("10.0.0.2")));
	zassert_is_null(set("mqtt_password", S("pw")));
	zassert_is_null(set("mqtt_prefix", S("x/y")));
	zassert_is_null(set("mqtt_commands", N(1)));
	zassert_is_null(set("net_hostname", S("nicht-live")));
	wf_sys_take_live(&a, &c);
	zassert_true(a.mqtt_enable && a.mqtt_commands);
	zassert_str_equal(a.mqtt_broker, "10.0.0.2");
	zassert_str_equal(a.mqtt_password, "pw");
	zassert_str_equal(a.mqtt_prefix, "x/y");
	zassert_str_equal(a.hostname, "", "Netzwerte erst nach Neustart");

	wf_sysconf_defaults(&a);
	zassert_is_null(wf_sys_repair(&a));
	a.mqtt_enable = true;
	zassert_not_null(wf_sys_repair(&a));
	zassert_false(a.mqtt_enable);
	zassert_true(a.dhcp, "DHCP bleibt");
	a.dhcp = false;
	a.ip = 0xC0A800FF; /* Broadcast */
	zassert_not_null(wf_sys_repair(&a));
	zassert_true(a.dhcp);
}

/* 10.1 meter_*, SOLL-MTR-01/02 */
ZTEST(api_sysconf, test_meter_params)
{
	zassert_equal(c.meter_type, 0, "Werkszustand: kein Zaehler");
	zassert_is_null(set("meter_type", N(4)));
	zassert_equal(c.meter_type, 4);
	zassert_not_null(set("meter_type", N(5)));
	zassert_not_null(set("meter_type", N(-1)));
	zassert_not_null(set("meter_type", S("1")));
	zassert_equal(c.meter_type, 4, "unveraendert nach Ablehnung");

	zassert_is_null(set("meter_address", N(247)));
	zassert_is_null(set("meter_address", N(1)));
	zassert_not_null(set("meter_address", N(0)), "0 = Broadcast");
	zassert_not_null(set("meter_address", N(248)));

	zassert_is_null(set("meter_baud", N(19200)));
	zassert_equal(c.meter_baud, 19200);
	zassert_is_null(set("meter_baud", N(115200)));
	zassert_not_null(set("meter_baud", N(1200)));
	zassert_not_null(set("meter_baud", N(9601)));

	zassert_is_null(set("meter_framing", N(3)));
	zassert_equal(c.meter_framing, WF_FRAMING_8N2);
	zassert_not_null(set("meter_framing", N(4)));
}

ZTEST(api_sysconf, test_ipv4_parse)
{
	uint32_t ip;
	char s[16];
	static const char *const bad[] = {"", "1.2.3", "1.2.3.4.5", "256.1.1.1", "01.2.3.4",
					  "1..2.3", "1.2.3.4 ", "a.b.c.d", "1.2.3.-4", "1234.1.1.1"};

	zassert_equal(wf_ipv4_parse("10.0.255.1", &ip), 0);
	zassert_equal(ip, 0x0A00FF01);
	wf_ipv4_format(ip, s);
	zassert_str_equal(s, "10.0.255.1");
	zassert_equal(wf_ipv4_parse("0.0.0.0", &ip), 0);
	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(wf_ipv4_parse(bad[i], &ip), -1, "%s", bad[i]);
	}
}

ZTEST(api_sysconf, test_set_rules)
{
	zassert_is_null(set("net_dhcp", N(0)));
	zassert_false(c.dhcp);
	zassert_not_null(set("net_dhcp", N(2)));
	zassert_not_null(set("net_dhcp", S("ja")));
	zassert_false(c.telnet, "F20: ab Werk aus");
	zassert_is_null(set("telnet_enable", (struct wf_jval){.type = WF_J_BOOL, .num = 1}));
	zassert_true(c.telnet);

	zassert_is_null(set("net_ip", S("10.1.2.3")));
	zassert_equal(c.ip, 0x0A010203);
	zassert_not_null(set("net_ip", S("127.0.0.1")));
	zassert_not_null(set("net_ip", S("224.0.0.1")));
	zassert_not_null(set("net_ip", S("0.0.0.0")));
	zassert_not_null(set("net_ip", N(1)));
	zassert_equal(c.ip, 0x0A010203, "unveraendert nach Ablehnung");

	zassert_is_null(set("net_netmask", S("255.255.0.0")));
	zassert_not_null(set("net_netmask", S("255.0.255.0")), "nicht zusammenhaengend");
	zassert_not_null(set("net_netmask", S("255.255.255.255")));
	zassert_not_null(set("net_netmask", S("254.0.0.0")));

	zassert_is_null(set("net_gateway", S("0.0.0.0")), "kein Gateway");
	zassert_is_null(set("net_gateway", S("10.1.0.1")));

	zassert_is_null(set("net_hostname", S("wallbox-garage")));
	zassert_str_equal(c.hostname, "wallbox-garage");
	zassert_is_null(set("net_hostname", S("")));
	zassert_str_equal(c.hostname, "");
	zassert_not_null(set("net_hostname", S("-x")));
	zassert_not_null(set("net_hostname", S("a b")));
	zassert_not_null(set("net_hostname", S("123456789012345678901234567890123")));
	zassert_not_null(set("net_hostname", N(1)));
}

ZTEST(api_sysconf, test_check)
{
	zassert_is_null(set("net_ip", S("192.168.0.255")));
	zassert_not_null(wf_sys_check(&c), "Broadcast");
	zassert_is_null(set("net_ip", S("192.168.0.0")));
	zassert_not_null(wf_sys_check(&c), "Netzadresse");
	zassert_is_null(set("net_ip", S("192.168.1.8")));
	zassert_not_null(wf_sys_check(&c), "Gateway ausserhalb");
	zassert_is_null(set("net_gateway", S("192.168.1.8")));
	zassert_not_null(wf_sys_check(&c), "Gateway = Adresse");
	zassert_is_null(set("net_gateway", S("0.0.0.0")));
	zassert_is_null(wf_sys_check(&c));
}

ZTEST(api_sysconf, test_encode_decode)
{
	struct wf_sysconf d;
	uint8_t buf[64];

	zassert_is_null(set("net_dhcp", N(0)));
	zassert_is_null(set("net_ip", S("10.0.0.5")));
	zassert_is_null(set("net_hostname", S("wb1")));
	zassert_is_null(set("meter_type", N(1)));
	zassert_is_null(set("meter_address", N(12)));
	zassert_is_null(set("meter_baud", N(38400)));
	zassert_is_null(set("meter_framing", N(1)));
	zassert_is_null(set("mqtt_enable", N(1)));
	zassert_is_null(set("mqtt_broker", S("192.168.1.50")));
	zassert_is_null(set("mqtt_port", N(1884)));
	zassert_is_null(set("mqtt_user", S("wb")));
	zassert_is_null(set("mqtt_password",
			    S("1234567890123456789012345678901234567890123456789012345678901234")));
	zassert_is_null(set("mqtt_prefix", S("a/b")));
	zassert_is_null(set("mqtt_interval", N(10)));
	zassert_is_null(set("mqtt_commands", N(1)));
	wf_sysconf_defaults(&d);
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		size_t n = wf_sys_encode(&c, p, buf);

		zassert_true(n >= 1 && n <= WF_SYS_VAL_MAX);
		zassert_equal(wf_sys_decode(&d, p->id, buf, n), 0, "%s", p->name);
	}
	zassert_mem_equal(&c, &d, sizeof(c));

	/* leerer Hostname */
	c.hostname[0] = '\0';
	size_t n = wf_sys_encode(&c, wf_sys_find("net_hostname"), buf);

	zassert_equal(n, 1);
	zassert_equal(wf_sys_decode(&d, 3014, buf, n), 0);
	zassert_str_equal(d.hostname, "");

	/* ungueltige gespeicherte Werte */
	zassert_equal(wf_sys_decode(&d, 3010, "\x02", 1), -1);
	zassert_equal(wf_sys_decode(&d, 3011, "\x7f\x00\x00\x01", 4), -1);
	zassert_equal(wf_sys_decode(&d, 3011, "\x0a\x00", 2), -1);
	zassert_equal(wf_sys_decode(&d, 3014, "a b", 3), -1);
	zassert_equal(wf_sys_decode(&d, 2999, "\x01", 1), -1);
	zassert_equal(wf_sys_decode(&d, 3016, "\x00\x00\x00\x05", 4), -1);
	zassert_equal(wf_sys_decode(&d, 3018, "\x00\x00\x04\xb0", 4), -1, "1200 Bd");
	zassert_equal(wf_sys_decode(&d, 3018, "\x00\x00\x4b\x00", 4), 0, "19200 Bd");
	zassert_equal(d.meter_baud, 19200);
	zassert_equal(wf_sys_decode(&d, 3017, "\x01", 1), -1);
	zassert_equal(wf_sys_decode(&d, 3022, "\x00\x00\x00\x00", 4), -1, "Port 0");
	zassert_equal(wf_sys_decode(&d, 3025, "a/#", 3), -1);
	zassert_equal(wf_sys_decode(&d, 3024, "\x00", 1), 0, "leeres Passwort");
	zassert_str_equal(d.mqtt_password, "");
}

/* ------------------------------------------------ Import */

static uint16_t orig[1000];
static bool have[1000];

static int lookup(void *ctx, uint16_t f, uint16_t *v)
{
	ARG_UNUSED(ctx);
	if (f >= 1000 || !have[f]) {
		return -1;
	}
	*v = orig[f];
	return 0;
}

static void put_ip(uint16_t first, int a, int b, int c2, int d)
{
	int v[4] = {a, b, c2, d};

	for (int i = 0; i < 4; i++) {
		orig[first + i] = (uint16_t)v[i];
		have[first + i] = true;
	}
}

ZTEST(api_sysconf, test_import)
{
	const char *err;

	memset(have, 0, sizeof(have));
	/* nichts gespeichert: 404 fehlt = 0 (DHCP aus), Adressen bleiben */
	zassert_true(c.dhcp);
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), 1);
	zassert_false(c.dhcp);
	zassert_equal(c.ip, 0xC0A80008, "Werkswert bleibt");

	/* fehlende Oktette = 0 (Testgeraet: 317, 322, 325 fehlen) */
	memset(have, 0, sizeof(have));
	for (int i = 0; i < 12; i++) {
		static const uint16_t v[12] = {192, 168, 0, 8, 255, 255, 255, 0, 192, 168, 0, 1};

		if (v[i] != 0) {
			orig[315 + i] = v[i];
			have[315 + i] = true;
		}
	}
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), 4);
	zassert_is_null(err);
	zassert_false(c.dhcp);
	zassert_equal(c.ip, 0xC0A80008);
	zassert_equal(c.netmask, 0xFFFFFF00);
	zassert_equal(c.gateway, 0xC0A80001);

	orig[404] = 0;
	have[404] = true;
	put_ip(315, 10, 0, 0, 20);
	put_ip(319, 255, 255, 255, 0);
	put_ip(323, 10, 0, 0, 1);
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), 4);
	zassert_is_null(err);
	zassert_false(c.dhcp);
	zassert_equal(c.ip, 0x0A000014);
	zassert_equal(c.gateway, 0x0A000001);

	/* Gateway ausserhalb: nichts uebernehmen */
	struct wf_sysconf before_import = c;

	put_ip(323, 192, 168, 9, 1);
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), -1);
	zassert_not_null(err);
	zassert_mem_equal(&c, &before_import, sizeof(c));

	put_ip(323, 10, 0, 0, 1);
	orig[317] = 300;
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), -1);
	orig[317] = 0;
	orig[404] = 7;
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), -1);
	orig[404] = 0x0101; /* oberes Byte ohne Bedeutung */
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), 4);
	zassert_true(c.dhcp);
}

/* mqtt_broker frueher als IPv4 unter 3021: beim Laden uebernehmen */
ZTEST(api_sysconf, test_migrate_broker)
{
	struct wf_sysconf a;
	const uint8_t old[4] = {192, 168, 1, 41}, none[4] = {0};

	wf_sysconf_defaults(&a);
	wf_sys_migrate_broker(&a, old, sizeof(old));
	zassert_str_equal(a.mqtt_broker, "192.168.1.41");
	wf_sys_migrate_broker(&a, (const uint8_t[]){10, 0, 0, 1}, 4);
	zassert_str_equal(a.mqtt_broker, "192.168.1.41", "neuer Wert hat Vorrang");
	wf_sysconf_defaults(&a);
	wf_sys_migrate_broker(&a, none, sizeof(none));
	zassert_str_equal(a.mqtt_broker, "", "0.0.0.0 = keiner");
	wf_sys_migrate_broker(&a, old, 3);
	zassert_str_equal(a.mqtt_broker, "", "falsche Laenge");
}
