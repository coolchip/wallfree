/* SPDX-License-Identifier: Apache-2.0 */
/* Tests Uhrzeit per SNTP ohne Netz (SOLL-TIME-01..04) */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/ntp.h>
#include <wallfree/api/sysconf.h>

ZTEST_SUITE(api_ntp, NULL, NULL, NULL, NULL, NULL);

static int parse(const char *s, char *host, uint16_t *port)
{
	return wf_ntp_parse_server(s, strlen(s), host, port);
}

ZTEST(api_ntp, test_parse_server)
{
	char host[65];
	uint16_t port;

	zassert_equal(parse("pool.ntp.org", host, &port), 0);
	zassert_str_equal(host, "pool.ntp.org");
	zassert_equal(port, 123);
	zassert_equal(parse("192.168.1.1:1123", host, &port), 0);
	zassert_str_equal(host, "192.168.1.1");
	zassert_equal(port, 1123);
	zassert_equal(parse("fritz.box", host, &port), 0);
	static const char *const bad[] = {"", ":123", "a b", "host:", "host:0", "host:65536",
					  "host:123456", "host:12a", ".host", "host.", "-a", "a_b"};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i], host, &port), -1, "%s", bad[i]);
	}
}

ZTEST(api_ntp, test_next)
{
	zassert_equal(wf_ntp_next_s(0), 6 * 3600, "nach Erfolg alle 6 h");
	zassert_equal(wf_ntp_next_s(1), 60);
	zassert_equal(wf_ntp_next_s(2), 120);
	zassert_equal(wf_ntp_next_s(6), 1920);
	zassert_equal(wf_ntp_next_s(7), 3600, "hoechstens stuendlich");
	zassert_equal(wf_ntp_next_s(1000), 3600);
}

ZTEST(api_ntp, test_epoch)
{
	zassert_equal(wf_ntp_epoch_ms(1793000000, 0x80000000u), 1793000000500);
	zassert_equal(wf_ntp_epoch_ms(0, 0), 0, "unplausibel (1970)");
	zassert_equal(wf_ntp_epoch_ms(1700000000, 0), 0, "vor 2025");
	zassert_equal(wf_ntp_epoch_ms(4102444800ull, 0), 0, "ab 2100");
}

ZTEST(api_ntp, test_rtc_kalender)
{
	struct wf_rtc_time t;

	wf_rtc_from_unix(1800000000ull, &t); /* Fr 2027-01-15 08:00:00 UTC */
	zassert_equal(t.year, 27);
	zassert_equal(t.month, 1);
	zassert_equal(t.day, 15);
	zassert_equal(t.weekday, 5, "Freitag");
	zassert_equal(t.hour, 8);
	zassert_equal(t.minute, 0);
	zassert_equal(t.second, 0);
	wf_rtc_from_unix(1835481599ull, &t); /* 2028-02-29 23:59:59 UTC, Schalttag */
	zassert_equal(t.month, 2);
	zassert_equal(t.day, 29);
	zassert_equal(t.hour, 23);
	zassert_equal(t.second, 59);
	for (uint64_t s = WF_NTP_MIN_EPOCH; s < WF_NTP_MAX_EPOCH; s += 86400ull * 13 + 4567) {
		wf_rtc_from_unix(s, &t);
		zassert_equal(wf_rtc_to_unix(&t), s, "Rundlauf %llu", s);
	}
}

ZTEST(api_ntp, test_rtc_unplausibel)
{
	struct wf_rtc_time t = {.year = 0, .month = 1, .day = 1, .weekday = 6};

	zassert_equal(wf_rtc_to_unix(&t), 0, "Kalender nie gestellt: 2000");
	t.year = 24;
	t.month = 12;
	t.day = 31;
	zassert_equal(wf_rtc_to_unix(&t), 0, "vor 2025");
	t.year = 27;
	t.month = 13;
	zassert_equal(wf_rtc_to_unix(&t), 0, "Monat");
	t.month = 1;
	t.hour = 24;
	zassert_equal(wf_rtc_to_unix(&t), 0, "Stunde");
}

/* Parameter: Werkszustand an, Server automatisch; Pruefung beim Setzen */
ZTEST(api_ntp, test_sysconf)
{
	struct wf_sysconf c;
	const struct wf_syspar *p = wf_sys_find("ntp_server");

	wf_sysconf_defaults(&c);
	zassert_true(c.ntp_enable);
	zassert_str_equal(c.ntp_server, "");
	zassert_not_null(p);
	zassert_is_null(wf_sys_set(&c, p, &(struct wf_jval){.type = WF_J_STR, .str = "ptbtime1.ptb.de",
							   .str_len = 15}));
	zassert_str_equal(c.ntp_server, "ptbtime1.ptb.de");
	zassert_not_null(wf_sys_set(&c, p, &(struct wf_jval){.type = WF_J_STR, .str = "a b",
							     .str_len = 3}));
	zassert_is_null(wf_sys_set(&c, p, &(struct wf_jval){.type = WF_J_STR, .str = "",
							   .str_len = 0}));
	zassert_true(wf_sys_find("ntp_server")->id >= WF_SYS_ID_LIVE, "wirkt sofort");
}
