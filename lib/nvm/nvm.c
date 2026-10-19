/* SPDX-License-Identifier: Apache-2.0 */
/* Parameterspeicher, Format siehe include/wallfree/nvm/nvm.h */
#include <string.h>
#include <wallfree/nvm/nvm.h>

#define ALIGN4(x)      (((x) + 3u) & ~3u)
#define ENT_SIZE(len)  (4u + ALIGN4(len) + 4u)
#define ENT_MAX        ENT_SIZE(NVM_MAX_LEN)
#define CHUNK          64u

struct ent {
	uint16_t id;
	uint8_t len;
	bool valid; /* CRC-32 stimmt */
	uint8_t data[NVM_MAX_LEN];
};

/* ------------------------------------------------------------ Pruefsummen */

uint8_t nvm_crc8(const uint8_t *p, size_t len)
{
	uint8_t c = 0;

	while (len--) {
		c ^= *p++;
		for (int i = 0; i < 8; i++) {
			c = (uint8_t)((c & 0x80u) ? (c << 1) ^ 0x07u : c << 1);
		}
	}
	return c;
}

uint32_t nvm_crc32(uint32_t crc, const uint8_t *p, size_t len)
{
	uint32_t c = ~crc;

	while (len--) {
		c ^= *p++;
		for (int i = 0; i < 8; i++) {
			c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
		}
	}
	return ~c;
}

/* ------------------------------------------------------------ Kodierung */

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

static uint16_t get16(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t get32(const uint8_t *p)
{
	return get16(p) | ((uint32_t)get16(p + 2) << 16);
}

static void make_area_hdr(uint8_t h[NVM_HDR_SIZE])
{
	put32(h, NVM_MAGIC);
	put16(h + 4, NVM_VERSION);
	put16(h + 6, (uint16_t)~NVM_VERSION);
}

static uint8_t hdr_crc(uint16_t id, uint8_t len)
{
	uint8_t b[3] = {(uint8_t)id, (uint8_t)(id >> 8), len};

	return nvm_crc8(b, sizeof(b));
}

/** Eintrag kodieren. @return Groesse in Byte */
static uint32_t encode(uint8_t *buf, uint16_t id, const void *data, uint8_t len)
{
	uint32_t pad = ALIGN4(len);

	put16(buf, id);
	buf[2] = len;
	buf[3] = hdr_crc(id, len);
	memcpy(buf + 4, data, len);
	memset(buf + 4 + len, 0xFF, pad - len);
	put32(buf + 4 + pad, nvm_crc32(0, buf, 4u + len));
	return ENT_SIZE(len);
}

/* ------------------------------------------------------------ Lesen */

static bool is_blank(const struct nvm *n, uint32_t off, uint32_t len, int *err)
{
	uint8_t b[CHUNK];

	while (len) {
		uint32_t k = len < CHUNK ? len : CHUNK;

		if (n->fl->read(n->fl->ctx, off, b, k) != 0) {
			*err = NVM_E_IO;
			return false;
		}
		for (uint32_t i = 0; i < k; i++) {
			if (b[i] != 0xFF) {
				return false;
			}
		}
		off += k;
		len -= k;
	}
	return true;
}

/**
 * Eintragskopf an @p off pruefen.
 * @return 1 gueltiger Kopf, 0 frei (0xFFFFFFFF), -1 unbrauchbar, NVM_E_IO
 */
static int read_hdr(const struct nvm *n, uint32_t off, uint16_t *id, uint8_t *len)
{
	uint8_t h[4];

	if (off + 4u > n->fl->size) {
		return -1;
	}
	if (n->fl->read(n->fl->ctx, off, h, 4) != 0) {
		return NVM_E_IO;
	}
	if (get32(h) == 0xFFFFFFFFu) {
		return 0;
	}
	*id = get16(h);
	*len = h[2];
	if (*id == NVM_ID_INVALID || *len == 0 || *len > NVM_MAX_LEN ||
	    h[3] != hdr_crc(*id, *len) || off + ENT_SIZE(*len) > n->fl->size) {
		return -1;
	}
	return 1;
}

/** Eintrag an @p off lesen (Kopf muss gueltig sein). */
static int read_ent(const struct nvm *n, uint32_t off, struct ent *e)
{
	uint8_t buf[ENT_MAX];
	uint16_t id;
	uint8_t len;
	int r = read_hdr(n, off, &id, &len);

	if (r < 0) {
		return r == NVM_E_IO ? NVM_E_IO : NVM_E_ARG;
	}
	if (n->fl->read(n->fl->ctx, off, buf, ENT_SIZE(len)) != 0) {
		return NVM_E_IO;
	}
	e->id = id;
	e->len = len;
	e->valid = get32(buf + 4 + ALIGN4(len)) == nvm_crc32(0, buf, 4u + len);
	memcpy(e->data, buf + 4, len);
	return (int)ENT_SIZE(len);
}

int nvm_mount(struct nvm *n, const struct nvm_flash *fl)
{
	uint8_t h[NVM_HDR_SIZE], want[NVM_HDR_SIZE];
	int err = NVM_OK;

	memset(n, 0, sizeof(*n));
	n->fl = fl;
	n->state = NVM_ST_UNMOUNTED;
	if (fl->size < NVM_HDR_SIZE + ENT_MAX || fl->read(fl->ctx, 0, h, sizeof(h)) != 0) {
		return NVM_E_IO;
	}
	make_area_hdr(want);
	if (memcmp(h, want, 4) != 0) {
		/* leer (erster Start) oder unterbrochenes Loeschen/fremdes Format */
		bool blank = is_blank(n, 0, fl->size, &err);

		if (err != NVM_OK) {
			return err;
		}
		n->state = blank ? NVM_ST_EMPTY : NVM_ST_INVALID;
		n->end = NVM_HDR_SIZE;
		return NVM_OK;
	}
	if (memcmp(h + 4, want + 4, 4) != 0) {
		/* Abschluss fehlt (Verdichten unterbrochen) oder andere Version */
		n->state = NVM_ST_INVALID;
		n->end = NVM_HDR_SIZE;
		return NVM_OK;
	}
	n->state = NVM_ST_OK;
	uint32_t off = NVM_HDR_SIZE;

	for (;;) {
		uint16_t id;
		uint8_t len;
		int r = read_hdr(n, off, &id, &len);

		if (r == NVM_E_IO) {
			return NVM_E_IO;
		}
		if (r == 0) {
			break; /* erste freie Stelle */
		}
		if (r < 0) {
			/* unterbrochener Kopf oder Bereichsende: Rest verbraucht */
			n->damaged = off + 4u <= fl->size;
			break;
		}
		off += ENT_SIZE(len);
	}
	n->end = off;
	return NVM_OK;
}

static struct nvm_pending *pend_find(struct nvm *n, uint16_t id)
{
	for (uint8_t i = 0; i < n->npend; i++) {
		if (n->pend[i].id == id) {
			return &n->pend[i];
		}
	}
	return NULL;
}

int nvm_read(struct nvm *n, uint16_t id, void *buf, size_t maxlen)
{
	if (n->state == NVM_ST_UNMOUNTED) {
		return NVM_E_STATE;
	}
	const struct nvm_pending *p = pend_find(n, id);

	if (p != NULL) {
		if (p->len > maxlen) {
			return NVM_E_ARG;
		}
		memcpy(buf, p->data, p->len);
		return p->len;
	}
	if (n->state != NVM_ST_OK) {
		return NVM_E_NOENT;
	}
	struct ent e;
	int found = NVM_E_NOENT;

	for (uint32_t off = NVM_HDR_SIZE; off < n->end;) {
		int r = read_ent(n, off, &e);

		if (r < 0) {
			return r;
		}
		if (e.valid && e.id == id) {
			if (e.len > maxlen) {
				return NVM_E_ARG;
			}
			memcpy(buf, e.data, e.len);
			found = e.len;
		}
		off += (uint32_t)r;
	}
	return found;
}

int nvm_foreach(struct nvm *n, nvm_visit_t fn, void *ctx)
{
	if (n->state == NVM_ST_UNMOUNTED) {
		return NVM_E_STATE;
	}
	if (n->state == NVM_ST_OK) {
		struct ent e;

		for (uint32_t off = NVM_HDR_SIZE; off < n->end;) {
			int r = read_ent(n, off, &e);

			if (r < 0) {
				return r;
			}
			if (e.valid) {
				fn(ctx, e.id, e.data, e.len);
			}
			off += (uint32_t)r;
		}
	}
	for (uint8_t i = 0; i < n->npend; i++) {
		fn(ctx, n->pend[i].id, n->pend[i].data, n->pend[i].len);
	}
	return NVM_OK;
}

/* ------------------------------------------------------------ Schreiben */

static int pend_put(struct nvm *n, uint16_t id, const void *data, uint8_t len)
{
	struct nvm_pending *p = pend_find(n, id);

	if (p == NULL) {
		if (n->npend >= NVM_PENDING_MAX) {
			return NVM_E_FULL;
		}
		p = &n->pend[n->npend++];
		p->id = id;
	}
	p->len = len;
	memcpy(p->data, data, len);
	return NVM_PENDING;
}

/** Anhaengen ohne Loeschen. @return NVM_OK, 1 = passt nicht, NVM_E_IO */
static int append(struct nvm *n, uint16_t id, const void *data, uint8_t len)
{
	uint8_t buf[ENT_MAX];
	uint32_t sz = ENT_SIZE(len);
	int err = NVM_OK;

	if (n->state == NVM_ST_INVALID || n->damaged || n->end + sz > n->fl->size) {
		return 1;
	}
	if (n->state == NVM_ST_EMPTY) {
		/* leerer Bereich: Kopf ohne Loeschen anlegen */
		uint8_t h[NVM_HDR_SIZE];

		make_area_hdr(h);
		if (n->fl->write(n->fl->ctx, 0, h, sizeof(h)) != 0) {
			n->state = NVM_ST_INVALID;
			return NVM_E_IO;
		}
		n->state = NVM_ST_OK;
	}
	/* nie ueber bereits programmierte Bytes schreiben */
	if (!is_blank(n, n->end, sz, &err)) {
		n->damaged = true;
		return err != NVM_OK ? err : 1;
	}
	encode(buf, id, data, len);
	if (n->fl->write(n->fl->ctx, n->end, buf, sz) != 0) {
		n->damaged = true;
		return NVM_E_IO;
	}
	n->end += sz;
	return NVM_OK;
}

int nvm_write(struct nvm *n, uint16_t id, const void *data, size_t len)
{
	uint8_t cur[NVM_MAX_LEN];

	if (id == NVM_ID_INVALID || len == 0 || len > NVM_MAX_LEN || data == NULL) {
		return NVM_E_ARG;
	}
	if (n->state == NVM_ST_UNMOUNTED) {
		return NVM_E_STATE;
	}
	int r = nvm_read(n, id, cur, sizeof(cur));

	if (r == (int)len && memcmp(cur, data, len) == 0) {
		return NVM_OK; /* unveraendert: kein Flashverschleiss */
	}
	if (pend_find(n, id) != NULL) {
		/* schon im RAM: dort bleibt der juengste Wert */
		return pend_put(n, id, data, (uint8_t)len);
	}
	r = append(n, id, data, (uint8_t)len);
	if (r == NVM_OK) {
		return NVM_OK;
	}
	int p = pend_put(n, id, data, (uint8_t)len);

	return r == NVM_E_IO ? NVM_E_IO : p;
}

bool nvm_needs_compact(const struct nvm *n, uint32_t reserve)
{
	return n->npend > 0 || n->damaged || n->state == NVM_ST_INVALID ||
	       (n->state == NVM_ST_OK && n->fl->size - n->end < reserve);
}

uint32_t nvm_used(const struct nvm *n)
{
	return n->state == NVM_ST_OK ? n->end : 0;
}

uint32_t nvm_free(const struct nvm *n)
{
	if (n->state == NVM_ST_INVALID || n->state == NVM_ST_UNMOUNTED || n->damaged) {
		return 0;
	}
	return n->fl->size - n->end;
}

/* ------------------------------------------------------------ Verdichten */

/* Arbeitspuffer: Folge von Datensaetzen [id u16][len u8][0][data, auf 4 aufgefuellt] */
struct coll {
	uint8_t *buf;
	size_t size;
	size_t used;
	bool overflow;
};

#define REC_SIZE(len) (4u + ALIGN4(len))

static void coll_put(struct coll *c, uint16_t id, const void *data, uint8_t len)
{
	for (size_t o = 0; o < c->used;) {
		uint8_t *r = c->buf + o;
		uint8_t rl = r[2];

		if (get16(r) == id) {
			if (rl == len) {
				memcpy(r + 4, data, len);
				return;
			}
			size_t rs = REC_SIZE(rl);

			memmove(r, r + rs, c->used - o - rs);
			c->used -= rs;
			break;
		}
		o += REC_SIZE(rl);
	}
	if (c->used + REC_SIZE(len) > c->size) {
		c->overflow = true;
		return;
	}
	uint8_t *r = c->buf + c->used;

	put16(r, id);
	r[2] = len;
	r[3] = 0;
	memcpy(r + 4, data, len);
	c->used += REC_SIZE(len);
}

static void coll_visit(void *ctx, uint16_t id, const void *data, size_t len)
{
	coll_put(ctx, id, data, (uint8_t)len);
}

static int write_area(struct nvm *n, const struct coll *c)
{
	uint8_t buf[ENT_MAX];
	uint8_t h[NVM_HDR_SIZE];
	uint32_t off = NVM_HDR_SIZE;

	make_area_hdr(h);
	if (n->fl->erase(n->fl->ctx) != 0) {
		return NVM_E_IO;
	}
	/* Kennung sofort, damit ein Ausfall danach als "unterbrochen" erkannt wird */
	if (n->fl->write(n->fl->ctx, 0, h, 4) != 0) {
		return NVM_E_IO;
	}
	for (size_t o = 0; o < c->used;) {
		const uint8_t *r = c->buf + o;
		uint8_t len = r[2];
		uint32_t sz = encode(buf, get16(r), r + 4, len);

		if (n->fl->write(n->fl->ctx, off, buf, sz) != 0) {
			return NVM_E_IO;
		}
		off += sz;
		o += REC_SIZE(len);
	}
	/* Abschluss zuletzt */
	if (n->fl->write(n->fl->ctx, 4, h + 4, 4) != 0) {
		return NVM_E_IO;
	}
	n->end = off;
	return NVM_OK;
}

int nvm_compact(struct nvm *n, void *scratch, size_t scratch_size)
{
	struct coll c = {.buf = scratch, .size = scratch_size};

	if (n->state == NVM_ST_UNMOUNTED) {
		return NVM_E_STATE;
	}
	/* ungueltiger Bereich: nur die RAM-Werte zaehlen (SOLL-NVM-05) */
	int r = nvm_foreach(n, coll_visit, &c);

	if (r != NVM_OK) {
		return r;
	}
	uint32_t need = NVM_HDR_SIZE;

	for (size_t o = 0; o < c.used; o += REC_SIZE(c.buf[o + 2])) {
		need += ENT_SIZE(c.buf[o + 2]);
	}
	if (c.overflow || need > n->fl->size) {
		return NVM_E_NOSPACE; /* nichts geloescht */
	}
	r = write_area(n, &c);
	n->damaged = false;
	if (r != NVM_OK) {
		/* RAM-Werte behalten; Bereich beim naechsten Start ungueltig */
		n->state = NVM_ST_INVALID;
		return r;
	}
	n->state = NVM_ST_OK;
	n->npend = 0;
	return NVM_OK;
}

int nvm_format(struct nvm *n)
{
	struct coll c = {0};

	if (n->state == NVM_ST_UNMOUNTED) {
		return NVM_E_STATE;
	}
	n->npend = 0;
	n->damaged = false;
	int r = write_area(n, &c);

	n->state = r == NVM_OK ? NVM_ST_OK : NVM_ST_INVALID;
	return r;
}
