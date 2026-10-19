/* SPDX-License-Identifier: Apache-2.0 */
/* MD5 nach RFC 1321, nur fuer HTTP-Digest (keine Sicherheitsfunktion darueber hinaus) */
#include <string.h>
#include <wallfree/api/digest.h>

static const uint32_t K[64] = {
	0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
	0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
	0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
	0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
	0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
	0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
	0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
	0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
	0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
	0xeb86d391,
};

static const uint8_t R[16] = {7, 12, 17, 22, 5, 9, 14, 20, 4, 11, 16, 23, 6, 10, 15, 21};

static uint32_t rol(uint32_t x, unsigned int c)
{
	return (x << c) | (x >> (32 - c));
}

static void block(uint32_t s[4], const uint8_t *p)
{
	uint32_t m[16];
	uint32_t a = s[0], b = s[1], c = s[2], d = s[3];

	for (int i = 0; i < 16; i++) {
		m[i] = (uint32_t)p[4 * i] | ((uint32_t)p[4 * i + 1] << 8) |
		       ((uint32_t)p[4 * i + 2] << 16) | ((uint32_t)p[4 * i + 3] << 24);
	}
	for (int i = 0; i < 64; i++) {
		uint32_t f;
		int g;

		switch (i / 16) {
		case 0:
			f = (b & c) | (~b & d);
			g = i;
			break;
		case 1:
			f = (d & b) | (~d & c);
			g = (5 * i + 1) % 16;
			break;
		case 2:
			f = b ^ c ^ d;
			g = (3 * i + 5) % 16;
			break;
		default:
			f = c ^ (b | ~d);
			g = (7 * i) % 16;
			break;
		}
		uint32_t t = d;

		d = c;
		c = b;
		b = b + rol(a + f + K[i] + m[g], R[(i / 16) * 4 + i % 4]);
		a = t;
	}
	s[0] += a;
	s[1] += b;
	s[2] += c;
	s[3] += d;
}

void wf_md5_init(struct wf_md5 *c)
{
	c->s[0] = 0x67452301;
	c->s[1] = 0xefcdab89;
	c->s[2] = 0x98badcfe;
	c->s[3] = 0x10325476;
	c->n = 0;
}

void wf_md5_update(struct wf_md5 *c, const void *data, size_t len)
{
	const uint8_t *p = data;
	size_t used = (size_t)(c->n % 64);

	c->n += len;
	while (len > 0) {
		size_t k = 64 - used < len ? 64 - used : len;

		memcpy(c->buf + used, p, k);
		used += k;
		p += k;
		len -= k;
		if (used == 64) {
			block(c->s, c->buf);
			used = 0;
		}
	}
}

void wf_md5_final(struct wf_md5 *c, uint8_t out[16])
{
	uint64_t bits = c->n * 8;
	uint8_t pad = 0x80;
	uint8_t len[8];

	wf_md5_update(c, &pad, 1);
	pad = 0;
	while (c->n % 64 != 56) {
		wf_md5_update(c, &pad, 1);
	}
	for (int i = 0; i < 8; i++) {
		len[i] = (uint8_t)(bits >> (8 * i));
	}
	wf_md5_update(c, len, 8);
	for (int i = 0; i < 16; i++) {
		out[i] = (uint8_t)(c->s[i / 4] >> (8 * (i % 4)));
	}
}

void wf_hex(const uint8_t *in, size_t len, char *out)
{
	static const char h[] = "0123456789abcdef";

	for (size_t i = 0; i < len; i++) {
		out[2 * i] = h[in[i] >> 4];
		out[2 * i + 1] = h[in[i] & 15];
	}
	out[2 * len] = '\0';
}
