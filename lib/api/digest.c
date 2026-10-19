/* SPDX-License-Identifier: Apache-2.0 */
/* HTTP-Digest (RFC 7616, MD5, qop=auth), siehe include/wallfree/api/digest.h */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wallfree/api/digest.h>

#define NONCE_LEN 24

struct params {
	char username[WF_DIGEST_USER_MAX + 1];
	char realm[33];
	char nonce[65]; /* auch fremde, laengere Nonces annehmen -> stale */
	char uri[160];
	char response[33];
	char qop[9];
	char nc[9];
	char cnonce[65];
	char algorithm[9];
};

static void md5_hex_parts(char out[33], const char *const *parts, size_t n)
{
	struct wf_md5 c;
	uint8_t d[16];

	wf_md5_init(&c);
	for (size_t i = 0; i < n; i++) {
		if (i > 0) {
			wf_md5_update(&c, ":", 1);
		}
		wf_md5_update(&c, parts[i], strlen(parts[i]));
	}
	wf_md5_final(&c, d);
	wf_hex(d, 16, out);
}

void wf_digest_ha1(const char *user, const char *realm, const char *password, uint8_t ha1[16])
{
	struct wf_md5 c;

	wf_md5_init(&c);
	wf_md5_update(&c, user, strlen(user));
	wf_md5_update(&c, ":", 1);
	wf_md5_update(&c, realm, strlen(realm));
	wf_md5_update(&c, ":", 1);
	wf_md5_update(&c, password, strlen(password));
	wf_md5_final(&c, ha1);
}

/* Nonce-Pruefteil: MD5(Geheimnis || Zeitstempel-Hex), erste 8 Byte */
static void nonce_mac(const struct wf_digest_cfg *cfg, const char ts[8], char out[17])
{
	struct wf_md5 c;
	uint8_t d[16];

	wf_md5_init(&c);
	wf_md5_update(&c, cfg->secret, WF_DIGEST_SECRET);
	wf_md5_update(&c, ts, 8);
	wf_md5_final(&c, d);
	wf_hex(d, 8, out);
}

int wf_digest_challenge(const struct wf_digest_cfg *cfg, uint32_t t, bool stale, char *out,
			size_t size)
{
	char nonce[NONCE_LEN + 1];

	snprintf(nonce, 9, "%08x", (unsigned int)t);
	nonce_mac(cfg, nonce, nonce + 8);
	int n = snprintf(out, size,
			 "Digest realm=\"%s\", qop=\"auth\", algorithm=MD5, nonce=\"%s\"%s",
			 cfg->realm, nonce, stale ? ", stale=true" : "");

	return n < 0 || (size_t)n >= size ? -1 : n;
}

/* ------------------------------------------------ Kopfzeile zerlegen */

static bool copy_val(char *dst, size_t size, const char *v, size_t len)
{
	if (len >= size) {
		return false;
	}
	memcpy(dst, v, len);
	dst[len] = '\0';
	return true;
}

static int parse(const char *h, struct params *p)
{
	memset(p, 0, sizeof(*p));
	while (*h == ' ') {
		h++;
	}
	if (strncasecmp(h, "Digest", 6) != 0 || (h[6] != ' ' && h[6] != '\t')) {
		return -1;
	}
	h += 6;
	for (;;) {
		while (*h == ' ' || *h == '\t' || *h == ',') {
			h++;
		}
		if (*h == '\0') {
			return 0;
		}
		const char *k = h;

		while (*h != '\0' && *h != '=' && *h != ' ' && *h != ',') {
			h++;
		}
		size_t klen = (size_t)(h - k);

		while (*h == ' ') {
			h++;
		}
		if (*h != '=') {
			return -1;
		}
		h++;
		while (*h == ' ') {
			h++;
		}
		char val[160];
		size_t vlen = 0;

		if (*h == '"') {
			h++;
			while (*h != '\0' && *h != '"') {
				if (*h == '\\' && h[1] != '\0') {
					h++;
				}
				if (vlen + 1 >= sizeof(val)) {
					return -1;
				}
				val[vlen++] = *h++;
			}
			if (*h != '"') {
				return -1;
			}
			h++;
		} else {
			while (*h != '\0' && *h != ',' && *h != ' ') {
				if (vlen + 1 >= sizeof(val)) {
					return -1;
				}
				val[vlen++] = *h++;
			}
		}
		val[vlen] = '\0';

		struct {
			const char *name;
			char *dst;
			size_t size;
		} f[] = {
			{"username", p->username, sizeof(p->username)},
			{"realm", p->realm, sizeof(p->realm)},
			{"nonce", p->nonce, sizeof(p->nonce)},
			{"uri", p->uri, sizeof(p->uri)},
			{"response", p->response, sizeof(p->response)},
			{"qop", p->qop, sizeof(p->qop)},
			{"nc", p->nc, sizeof(p->nc)},
			{"cnonce", p->cnonce, sizeof(p->cnonce)},
			{"algorithm", p->algorithm, sizeof(p->algorithm)},
		};
		for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
			if (strlen(f[i].name) == klen && strncasecmp(k, f[i].name, klen) == 0) {
				if (!copy_val(f[i].dst, f[i].size, val, vlen)) {
					return -1;
				}
				break;
			}
		}
		/* unbekannte Parameter (opaque, userhash ...) werden ignoriert */
	}
}

static bool is_hex(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		if (!isxdigit((unsigned char)s[i])) {
			return false;
		}
	}
	return s[n] == '\0';
}

enum wf_auth wf_digest_check(const struct wf_digest_cfg *cfg, const char *hdr, const char *method,
			     const char *uri)
{
	struct params p;

	if (hdr == NULL || *hdr == '\0') {
		return WF_AUTH_MISSING;
	}
	if (parse(hdr, &p) != 0 || p.response[0] == '\0' || p.nonce[0] == '\0' ||
	    p.cnonce[0] == '\0' || !is_hex(p.nc, 8) || !is_hex(p.response, 32) ||
	    strcmp(p.qop, "auth") != 0 ||
	    (p.algorithm[0] != '\0' && strcasecmp(p.algorithm, "MD5") != 0) ||
	    strcmp(p.username, cfg->user) != 0 || strcmp(p.realm, cfg->realm) != 0 ||
	    strcmp(p.uri, uri) != 0) {
		return WF_AUTH_BAD;
	}
	char ha1[33], ha2[33], expect[33];
	const char *a2[] = {method, uri};

	wf_hex(cfg->ha1, 16, ha1);
	md5_hex_parts(ha2, a2, 2);
	const char *r[] = {ha1, p.nonce, p.nc, p.cnonce, p.qop, ha2};

	md5_hex_parts(expect, r, 6);

	/* Vergleich ohne fruehen Abbruch, Gross-/Kleinschreibung egal */
	uint8_t diff = 0;

	for (int i = 0; i < 32; i++) {
		diff |= (uint8_t)(expect[i] ^ (char)tolower((unsigned char)p.response[i]));
	}
	if (diff != 0) {
		return WF_AUTH_BAD;
	}
	char mac[17];

	if (strlen(p.nonce) != NONCE_LEN || !is_hex(p.nonce, NONCE_LEN)) {
		return WF_AUTH_STALE;
	}
	nonce_mac(cfg, p.nonce, mac);
	return strcmp(mac, p.nonce + 8) == 0 ? WF_AUTH_OK : WF_AUTH_STALE;
}
