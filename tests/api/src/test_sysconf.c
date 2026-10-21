/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Netz- und Systemparameter (10.1 net_*, telnet_enable, meter_*; F20, F25, F28; SOLL-NVM-10) */
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
	char buf[320];
	struct wf_jbuf b;

	wf_jb_init(&b, buf, sizeof(buf));
	wf_sys_json(&c, &b);
	zassert_str_equal(buf, "{\"net_dhcp\":true,\"net_ip\":\"192.168.0.8\","
			       "\"net_netmask\":\"255.255.255.0\",\"net_gateway\":\"192.168.0.1\","
			       "\"net_hostname\":\"\",\"telnet_enable\":true,\"meter_type\":0,"
			       "\"meter_address\":1,\"meter_baud\":9600,\"meter_framing\":0}");
	zassert_is_null(wf_sys_check(&c));
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);

		zassert_true(p->id >= WF_SYS_ID_FIRST);
		zassert_equal(wf_sys_by_id(p->id), p);
	}
	zassert_is_null(wf_sys_at(wf_sys_count()));
	zassert_is_null(wf_sys_find("nix"));
	zassert_is_null(wf_sys_by_id(1));

	char info[1536];

	wf_jb_init(&b, info, sizeof(info));
	wf_sys_info_json(&b);
	zassert_false(b.overflow);
	zassert_not_null(strstr(info, "{\"name\":\"net_ip\",\"type\":\"ipv4\""));
	zassert_not_null(strstr(info, "{\"name\":\"meter_type\",\"type\":\"code\""));
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
	zassert_is_null(set("telnet_enable", (struct wf_jval){.type = WF_J_BOOL, .num = 0}));
	zassert_false(c.telnet);

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
	wf_sysconf_defaults(&d);
	for (size_t i = 0; i < wf_sys_count(); i++) {
		const struct wf_syspar *p = wf_sys_at(i);
		size_t n = wf_sys_encode(&c, p, buf);

		zassert_true(n >= 1 && n <= WF_HOSTNAME_MAX);
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
	zassert_equal(wf_sys_import_orig(&c, lookup, NULL, &err), 0, "nichts gespeichert");

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
