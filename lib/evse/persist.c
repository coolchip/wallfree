/* SPDX-License-Identifier: Apache-2.0 */
/* Persistenz der Ladelogik, siehe include/wallfree/evse/persist.h */
#include <wallfree/evse/persist.h>
#include <wallfree/nvm/orig.h>

static void enc32(uint8_t b[4], int32_t v)
{
	uint32_t u = (uint32_t)v;

	b[0] = (uint8_t)u;
	b[1] = (uint8_t)(u >> 8);
	b[2] = (uint8_t)(u >> 16);
	b[3] = (uint8_t)(u >> 24);
}

static int32_t dec32(const uint8_t b[4])
{
	return (int32_t)(b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
			 ((uint32_t)b[3] << 24));
}

struct load_ctx {
	struct evse_config *cfg;
	enum evse_lock_state *lock;
	struct evse_load_result *res;
};

static void load_visit(void *ctx, uint16_t id, const void *data, size_t len)
{
	struct load_ctx *c = ctx;

	if (id >= EVSE_NVM_ID_APP) {
		return; /* Anwendungsparameter */
	}
	if (len != 4) {
		c->res->rejected++;
		return;
	}
	int32_t v = dec32(data);

	if (id == EVSE_NVM_ID_LOCK) {
		if (v == EVSE_LOCK_LOCKED || v == EVSE_LOCK_UNLOCKED) {
			*c->lock = (enum evse_lock_state)v;
			c->res->restored++;
		} else {
			c->res->rejected++;
		}
		return;
	}
	switch (evse_param_restore(c->cfg, id, v)) {
	case EVSE_PARAM_OK:
		c->res->restored++;
		break;
	case EVSE_PARAM_E_UNKNOWN:
		c->res->unknown++;
		break;
	default:
		c->res->rejected++;
		break;
	}
}

int evse_persist_load(struct nvm *n, struct evse_config *cfg, enum evse_lock_state *lock,
		      struct evse_load_result *res)
{
	struct load_ctx c = {.cfg = cfg, .lock = lock, .res = res};

	*res = (struct evse_load_result){0};
	/* der Reihe nach: am Ende gilt jeweils der juengste gueltige Wert */
	return nvm_foreach(n, load_visit, &c);
}

int evse_persist_param(struct nvm *n, const struct evse_param *p, int32_t v)
{
	uint8_t b[4];

	if (!evse_param_persistent(p)) {
		return NVM_OK; /* Laufzeitwerte nie speichern (SOLL-NVM-03) */
	}
	enc32(b, v);
	return nvm_write(n, p->id, b, sizeof(b));
}

int evse_persist_lock(struct nvm *n, enum evse_lock_state st)
{
	uint8_t b[4];

	if (st != EVSE_LOCK_LOCKED && st != EVSE_LOCK_UNLOCKED) {
		return NVM_OK;
	}
	enc32(b, st);
	return nvm_write(n, EVSE_NVM_ID_LOCK, b, sizeof(b));
}

bool evse_flash_erase_allowed(const struct evse *e)
{
	return e->state == EVSE_S1_IDLE && e->status == EVSE_CP_A && !e->contactor &&
	       !e->out.contactor && e->lock.phase == EVSE_LOCK_PH_IDLE &&
	       e->rcm.test != EVSE_RCM_TEST_RUNNING && e->rcm.test != EVSE_RCM_TEST_ABORTING;
}

/* ------------------------------------------------ Import aus Sektor 3 */

/* SOLL-NVM-10: Feldauswahl */
static const uint16_t import_fields[] = {
	327, 328, 329, 330, 390, 409, 412, 519, 520, 521, 522, 523, 524, 525, 526, 527,
};
#define NET_FIRST 315 /* IP, Netzmaske, Gateway je Byte (Halbwort) */
#define NET_LAST  326

static const struct evse_param *param_by_field(uint16_t field)
{
	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);

		if (p->orig_field == field && p->kind == EVSE_PK_CONFIG) {
			return p;
		}
	}
	return NULL;
}

int evse_import_orig(struct evse *e, const struct nvm_flash *orig, struct nvm *n, evse_ms_t now,
		     evse_import_report_t report, void *ctx)
{
	uint16_t raw;
	int done = 0;

	if (!evse_flash_erase_allowed(e)) {
		return EVSE_IMP_E_UNSAFE;
	}
	int r = nvm_orig_read(orig, import_fields[0], &raw);

	if (r == NVM_E_STATE) {
		return EVSE_IMP_E_ORIG;
	}
	if (r == NVM_E_IO) {
		return NVM_E_IO;
	}
	for (size_t i = 0; i < sizeof(import_fields) / sizeof(import_fields[0]); i++) {
		uint16_t f = import_fields[i];
		const struct evse_param *p = param_by_field(f);

		r = nvm_orig_read(orig, f, &raw);
		if (r == NVM_E_IO) {
			return NVM_E_IO;
		}
		if (r != NVM_OK) {
			report(ctx, f, p->name, 0, EVSE_IMP_MISSING, 0);
			continue;
		}
		/* 409/412 sind im Original 1-Byte-Felder: unteres Byte des Halbworts */
		int32_t v = (f == 409 || f == 412) ? (raw & 0xFF) : raw;
		int err = evse_param_set(e, p, v, now);

		if (err != EVSE_PARAM_OK) {
			report(ctx, f, p->name, v, EVSE_IMP_REJECTED, err);
			continue;
		}
		err = evse_persist_param(n, p, v);
		if (err < 0) {
			report(ctx, f, p->name, v, EVSE_IMP_NOT_SAVED, err);
		} else {
			report(ctx, f, p->name, v, EVSE_IMP_DONE, err);
		}
		done++;
	}
	for (uint16_t f = NET_FIRST; f <= NET_LAST; f++) {
		r = nvm_orig_read(orig, f, &raw);
		if (r == NVM_OK) {
			report(ctx, f, "net", raw, EVSE_IMP_UNSUPPORTED, 0);
		}
	}
	return done;
}
