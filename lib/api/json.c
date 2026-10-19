/* SPDX-License-Identifier: Apache-2.0 */
/* Minimales JSON, siehe include/wallfree/api/json.h */
#include <stdio.h>
#include <string.h>
#include <wallfree/api/json.h>

void wf_jb_init(struct wf_jbuf *b, char *p, size_t size)
{
	b->p = p;
	b->size = size;
	b->len = 0;
	b->overflow = false;
	if (size > 0) {
		p[0] = '\0';
	}
}

static void put(struct wf_jbuf *b, const char *s, size_t n)
{
	if (b->overflow || b->len + n + 1 > b->size) {
		b->overflow = true;
		return;
	}
	memcpy(b->p + b->len, s, n);
	b->len += n;
	b->p[b->len] = '\0';
}

void wf_jb_raw(struct wf_jbuf *b, const char *s)
{
	put(b, s, strlen(s));
}

void wf_jb_str(struct wf_jbuf *b, const char *s)
{
	put(b, "\"", 1);
	for (; *s; s++) {
		unsigned char c = (unsigned char)*s;
		char esc[8];

		if (c == '"' || c == '\\') {
			esc[0] = '\\';
			esc[1] = (char)c;
			put(b, esc, 2);
		} else if (c < 0x20) {
			snprintf(esc, sizeof(esc), "\\u%04x", c);
			put(b, esc, 6);
		} else {
			put(b, (const char *)&c, 1);
		}
	}
	put(b, "\"", 1);
}

void wf_jb_int(struct wf_jbuf *b, int64_t v)
{
	char t[24];
	int n = snprintf(t, sizeof(t), "%lld", (long long)v);

	put(b, t, (size_t)n);
}

void wf_jb_key(struct wf_jbuf *b, const char *name, bool first)
{
	if (!first) {
		put(b, ",", 1);
	}
	wf_jb_str(b, name);
	put(b, ":", 1);
}

/* ------------------------------------------------------------ Lesen */

struct rd {
	const char *s;
	const char *end;
};

static void ws(struct rd *r)
{
	while (r->s < r->end && (*r->s == ' ' || *r->s == '\t' || *r->s == '\r' || *r->s == '\n')) {
		r->s++;
	}
}

static bool lit(struct rd *r, const char *w)
{
	size_t n = strlen(w);

	if ((size_t)(r->end - r->s) >= n && memcmp(r->s, w, n) == 0) {
		r->s += n;
		return true;
	}
	return false;
}

static int hexval(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	return -1;
}

/* Text nach @p out entpacken (nur ASCII aus \u00xx; anderes wird abgelehnt) */
static int str(struct rd *r, char **out, size_t *room, const char **res, size_t *res_len)
{
	if (r->s >= r->end || *r->s != '"') {
		return -1;
	}
	r->s++;
	*res = *out;
	size_t n = 0;

	while (r->s < r->end && *r->s != '"') {
		char c = *r->s++;

		if ((unsigned char)c < 0x20) {
			return -1;
		}
		if (c == '\\') {
			if (r->s >= r->end) {
				return -1;
			}
			char e = *r->s++;

			switch (e) {
			case '"':
			case '\\':
			case '/':
				c = e;
				break;
			case 'n':
				c = '\n';
				break;
			case 't':
				c = '\t';
				break;
			case 'u': {
				int v = 0;

				for (int i = 0; i < 4; i++) {
					int h = r->s < r->end ? hexval(*r->s++) : -1;

					if (h < 0) {
						return -1;
					}
					v = v * 16 + h;
				}
				if (v == 0 || v > 0x7F) {
					return -1;
				}
				c = (char)v;
				break;
			}
			default:
				return -1;
			}
		}
		if (n + 1 >= *room) {
			return -1;
		}
		(*out)[n++] = c;
	}
	if (r->s >= r->end) {
		return -1;
	}
	r->s++;
	(*out)[n] = '\0';
	*res_len = n;
	*out += n + 1;
	*room -= n + 1;
	return 0;
}

static int num(struct rd *r, int64_t *out)
{
	bool neg = false;
	int64_t v = 0;
	int digits = 0;

	if (r->s < r->end && *r->s == '-') {
		neg = true;
		r->s++;
	}
	while (r->s < r->end && *r->s >= '0' && *r->s <= '9') {
		if (v > (INT64_MAX - 9) / 10) {
			return -1;
		}
		v = v * 10 + (*r->s++ - '0');
		digits++;
	}
	/* nur ganze Zahlen; Bruch oder Exponent wird abgelehnt */
	if (digits == 0 || (r->s < r->end && (*r->s == '.' || *r->s == 'e' || *r->s == 'E'))) {
		return -1;
	}
	*out = neg ? -v : v;
	return 0;
}

int wf_json_object(const char *s, size_t len, char *strbuf, size_t strbuf_size,
		   wf_json_visit_t fn, void *ctx)
{
	struct rd r = {s, s + len};
	char *out = strbuf;
	size_t room = strbuf_size;

	ws(&r);
	if (!lit(&r, "{")) {
		return -1;
	}
	ws(&r);
	if (lit(&r, "}")) {
		ws(&r);
		return r.s == r.end ? 0 : -1;
	}
	for (;;) {
		const char *key;
		size_t klen;
		struct wf_jval v = {0};

		ws(&r);
		if (str(&r, &out, &room, &key, &klen) != 0) {
			return -1;
		}
		ws(&r);
		if (!lit(&r, ":")) {
			return -1;
		}
		ws(&r);
		if (r.s < r.end && *r.s == '"') {
			v.type = WF_J_STR;
			if (str(&r, &out, &room, &v.str, &v.str_len) != 0) {
				return -1;
			}
		} else if (lit(&r, "true")) {
			v.type = WF_J_BOOL;
			v.num = 1;
		} else if (lit(&r, "false")) {
			v.type = WF_J_BOOL;
		} else if (lit(&r, "null")) {
			v.type = WF_J_NULL;
		} else if (num(&r, &v.num) == 0) {
			v.type = WF_J_NUM;
		} else {
			return -1;
		}
		int rc = fn(ctx, key, &v);

		if (rc != 0) {
			return rc;
		}
		ws(&r);
		if (lit(&r, ",")) {
			continue;
		}
		if (!lit(&r, "}")) {
			return -1;
		}
		ws(&r);
		return r.s == r.end ? 0 : -1;
	}
}

int wf_json_number(const char *s, size_t len, int64_t *out)
{
	struct rd r = {s, s + len};

	ws(&r);
	if (num(&r, out) != 0) {
		return -1;
	}
	ws(&r);
	return r.s == r.end ? 0 : -1;
}
