/* SPDX-License-Identifier: Apache-2.0 */
/* Tests MD5, Digest (SOLL-API-07) und JSON */
#include <stdio.h>
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/digest.h>
#include <wallfree/api/json.h>

ZTEST_SUITE(api_md5, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(api_digest, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(api_json, NULL, NULL, NULL, NULL, NULL);

static void md5s(const char *s, char out[33])
{
	struct wf_md5 c;
	uint8_t d[16];

	wf_md5_init(&c);
	wf_md5_update(&c, s, strlen(s));
	wf_md5_final(&c, d);
	wf_hex(d, 16, out);
}

/* Pruefvektoren RFC 1321 Anhang A.5 */
ZTEST(api_md5, test_rfc1321_vectors)
{
	static const char *const v[][2] = {
		{"", "d41d8cd98f00b204e9800998ecf8427e"},
		{"a", "0cc175b9c0f1b6a831c399e269772661"},
		{"abc", "900150983cd24fb0d6963f7d28e17f72"},
		{"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
		{"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"},
		{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789",
		 "d174ab98d277d9f5a5611c2c9f419d9f"},
		{"1234567890123456789012345678901234567890123456789012345678901234567890"
		 "1234567890",
		 "57edf4a22be3c955ac49da2e2107b67a"},
	};
	char h[33];

	for (size_t i = 0; i < ARRAY_SIZE(v); i++) {
		md5s(v[i][0], h);
		zassert_str_equal(h, v[i][1], "%zu", i);
	}
}

ZTEST(api_md5, test_incremental)
{
	struct wf_md5 c;
	uint8_t d[16];
	char h[33];
	const char *s = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

	wf_md5_init(&c);
	for (size_t i = 0; i < strlen(s); i += 7) {
		size_t k = strlen(s) - i < 7 ? strlen(s) - i : 7;

		wf_md5_update(&c, s + i, k);
	}
	wf_md5_final(&c, d);
	wf_hex(d, 16, h);
	zassert_str_equal(h, "d174ab98d277d9f5a5611c2c9f419d9f");
}

/* ------------------------------------------------ Digest */

static struct wf_digest_cfg cfg;

static void setup_cfg(const char *user, const char *realm, const char *pw)
{
	memset(&cfg, 0, sizeof(cfg));
	cfg.realm = realm;
	cfg.user = user;
	wf_digest_ha1(user, realm, pw, cfg.ha1);
	for (int i = 0; i < WF_DIGEST_SECRET; i++) {
		cfg.secret[i] = (uint8_t)(i * 13 + 7);
	}
}

/* Antwort wie ein Client (RFC 7616 3.4.1) */
static void client_header(char *out, size_t size, const char *user, const char *realm,
			  const char *pw, const char *nonce, const char *method, const char *uri,
			  const char *nc, const char *cnonce)
{
	char a1[160], a2[160], ha1[33], ha2[33], r[300], resp[33];

	snprintf(a1, sizeof(a1), "%s:%s:%s", user, realm, pw);
	md5s(a1, ha1);
	snprintf(a2, sizeof(a2), "%s:%s", method, uri);
	md5s(a2, ha2);
	snprintf(r, sizeof(r), "%s:%s:%s:%s:auth:%s", ha1, nonce, nc, cnonce, ha2);
	md5s(r, resp);
	snprintf(out, size,
		 "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
		 "cnonce=\"%s\", nc=%s, qop=auth, response=\"%s\", algorithm=MD5",
		 user, realm, nonce, uri, cnonce, nc, resp);
}

static void nonce_from_challenge(const char *ch, char nonce[32])
{
	const char *p = strstr(ch, "nonce=\"");

	zassert_not_null(p);
	p += 7;
	size_t n = strcspn(p, "\"");

	zassert_true(n < 32);
	memcpy(nonce, p, n);
	nonce[n] = '\0';
}

/* Beispiel aus RFC 2617 3.5: Antwort stimmt, Nonce ist nicht von uns -> stale */
ZTEST(api_digest, test_rfc2617_example)
{
	setup_cfg("Mufasa", "testrealm@host.com", "Circle Of Life");
	const char *h = "Digest username=\"Mufasa\", realm=\"testrealm@host.com\", "
			"nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", uri=\"/dir/index.html\", "
			"qop=auth, nc=00000001, cnonce=\"0a4f113b\", "
			"response=\"6629fae49393a05397450978507c4ef1\", "
			"opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"";

	zassert_equal(wf_digest_check(&cfg, h, "GET", "/dir/index.html"), WF_AUTH_STALE);
	zassert_equal(wf_digest_check(&cfg, h, "POST", "/dir/index.html"), WF_AUTH_BAD);
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/other"), WF_AUTH_BAD);
}

ZTEST(api_digest, test_roundtrip)
{
	char ch[200], nonce[32], h[400];

	setup_cfg("admin", WF_DIGEST_REALM, "geheim123");
	zassert_true(wf_digest_challenge(&cfg, 0x1234, false, ch, sizeof(ch)) > 0);
	zassert_not_null(strstr(ch, "Digest realm=\"wallfree\""));
	zassert_not_null(strstr(ch, "qop=\"auth\""));
	zassert_is_null(strstr(ch, "stale"));
	nonce_from_challenge(ch, nonce);
	zassert_equal(strlen(nonce), 24);

	client_header(h, sizeof(h), "admin", "wallfree", "geheim123", nonce, "GET", "/evse/state",
		      "00000001", "abcdef0123456789");
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/evse/state"), WF_AUTH_OK);
	/* WebSocket-Aufbau mit vorab gesendeter Anmeldung (evcc) */
	client_header(h, sizeof(h), "admin", "wallfree", "geheim123", nonce, "GET", "/ws",
		      "00000002", "1122");
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/ws"), WF_AUTH_OK);

	/* falsches Passwort, falscher Benutzer */
	client_header(h, sizeof(h), "admin", "wallfree", "falsch", nonce, "GET", "/evse/state",
		      "00000001", "abcd");
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/evse/state"), WF_AUTH_BAD);
	client_header(h, sizeof(h), "root", "wallfree", "geheim123", nonce, "GET", "/evse/state",
		      "00000001", "abcd");
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/evse/state"), WF_AUTH_BAD);

	/* Nonce eines frueheren Starts (anderes Geheimnis): stale */
	client_header(h, sizeof(h), "admin", "wallfree", "geheim123", nonce, "GET", "/evse/state",
		      "00000001", "abcd");
	cfg.secret[0] ^= 1;
	zassert_equal(wf_digest_check(&cfg, h, "GET", "/evse/state"), WF_AUTH_STALE);
	zassert_true(wf_digest_challenge(&cfg, 1, true, ch, sizeof(ch)) > 0);
	zassert_not_null(strstr(ch, "stale=true"));
}

ZTEST(api_digest, test_malformed)
{
	setup_cfg("admin", WF_DIGEST_REALM, "geheim123");
	zassert_equal(wf_digest_check(&cfg, NULL, "GET", "/"), WF_AUTH_MISSING);
	zassert_equal(wf_digest_check(&cfg, "", "GET", "/"), WF_AUTH_MISSING);
	zassert_equal(wf_digest_check(&cfg, "Basic YWRtaW46eA==", "GET", "/"), WF_AUTH_BAD);
	zassert_equal(wf_digest_check(&cfg, "Digest", "GET", "/"), WF_AUTH_BAD);
	zassert_equal(wf_digest_check(&cfg, "Digest username=\"admin", "GET", "/"), WF_AUTH_BAD);
	zassert_equal(wf_digest_check(&cfg, "Digest username", "GET", "/"), WF_AUTH_BAD);

	char big[400];

	memset(big, 'a', sizeof(big));
	memcpy(big, "Digest username=\"", 17);
	big[sizeof(big) - 1] = '\0';
	zassert_equal(wf_digest_check(&cfg, big, "GET", "/"), WF_AUTH_BAD, "zu lang");

	/* ohne qop (RFC 2069) und mit anderem Verfahren abgelehnt */
	zassert_equal(wf_digest_check(&cfg,
				      "Digest username=\"admin\", realm=\"wallfree\", nonce=\"x\", "
				      "uri=\"/\", response=\"00000000000000000000000000000000\"",
				      "GET", "/"),
		      WF_AUTH_BAD);
	zassert_equal(wf_digest_check(&cfg,
				      "Digest username=\"admin\", realm=\"wallfree\", nonce=\"x\", "
				      "uri=\"/\", qop=auth, nc=00000001, cnonce=\"c\", "
				      "algorithm=SHA-256, "
				      "response=\"00000000000000000000000000000000\"",
				      "GET", "/"),
		      WF_AUTH_BAD);
}

ZTEST(api_digest, test_challenge_buffer)
{
	char small[20];

	setup_cfg("admin", WF_DIGEST_REALM, "x");
	zassert_equal(wf_digest_challenge(&cfg, 1, false, small, sizeof(small)), -1);
}

/* ------------------------------------------------ JSON */

static char seen[8][32];
static struct wf_jval seen_v[8];
static int nseen;

static int collect(void *ctx, const char *key, const struct wf_jval *v)
{
	ARG_UNUSED(ctx);
	zassert_true(nseen < 8);
	strncpy(seen[nseen], key, sizeof(seen[0]) - 1);
	seen_v[nseen++] = *v;
	return 0;
}

static int parse(const char *s)
{
	char sb[128];

	nseen = 0;
	return wf_json_object(s, strlen(s), sb, sizeof(sb), collect, NULL);
}

ZTEST(api_json, test_object)
{
	zassert_equal(parse(" { \"a\" : 1, \"b\":-20000,\"c\":true,\"d\":false,\"e\":null,"
			    "\"f\":\"x\\\"y\\u0041\" } "),
		      0);
	zassert_equal(nseen, 6);
	zassert_str_equal(seen[0], "a");
	zassert_equal(seen_v[0].num, 1);
	zassert_equal(seen_v[1].num, -20000);
	zassert_equal(seen_v[2].type, WF_J_BOOL);
	zassert_equal(seen_v[2].num, 1);
	zassert_equal(seen_v[3].num, 0);
	zassert_equal(seen_v[4].type, WF_J_NULL);
	zassert_equal(seen_v[5].type, WF_J_STR);
	zassert_equal(parse("{}"), 0);
	zassert_equal(nseen, 0);
}

ZTEST(api_json, test_rejects)
{
	static const char *const bad[] = {
		"", "[]", "{", "{\"a\"}", "{\"a\":}", "{\"a\":1,}", "{\"a\":1} x", "{\"a\":1.5}",
		"{\"a\":1e3}", "{\"a\":{\"b\":1}}", "{\"a\":[1]}", "{\"a\":\"\\u00e4\"}",
		"{\"a\":\"\\q\"}", "{a:1}", "{\"a\":99999999999999999999}", "{\"a\":-}",
		"{\"a\":\"x\ny\"}",
	};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(parse(bad[i]), -1, "%s", bad[i]);
	}
	/* Textpuffer zu klein */
	char sb[4];

	zassert_equal(wf_json_object("{\"abcdef\":1}", 12, sb, sizeof(sb), collect, NULL), -1);
}

ZTEST(api_json, test_number)
{
	int64_t v;

	zassert_equal(wf_json_number(" 16000 ", 7, &v), 0);
	zassert_equal(v, 16000);
	zassert_equal(wf_json_number("16000x", 6, &v), -1);
	zassert_equal(wf_json_number("", 0, &v), -1);
}

ZTEST(api_json, test_writer)
{
	char b[64];
	struct wf_jbuf j;

	wf_jb_init(&j, b, sizeof(b));
	wf_jb_raw(&j, "{");
	wf_jb_key(&j, "a\"b", true);
	wf_jb_str(&j, "x\\y\n");
	wf_jb_key(&j, "n", false);
	wf_jb_int(&j, -5);
	wf_jb_raw(&j, "}");
	zassert_false(j.overflow);
	zassert_str_equal(b, "{\"a\\\"b\":\"x\\\\y\\u000a\",\"n\":-5}");

	char s[8];

	wf_jb_init(&j, s, sizeof(s));
	wf_jb_str(&j, "zu lang fuer den Puffer");
	zassert_true(j.overflow);
	zassert_true(strlen(s) < sizeof(s));
}
