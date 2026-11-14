/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Zaehlerabfrage (Soll-Spezifikation 12.5, SOLL-MTR-05..15, 30/31) mit einem
 * gescripteten Bus: je Anfrage ein Ergebnis aus der Liste; ein "gutes"
 * Ergebnis liefert das hinterlegte Registerbild des Blocks.
 */
#include <string.h>
#include <zephyr/ztest.h>
#include <wallfree/api/meter_json.h>
#include <wallfree/meter/meter.h>

#define CALLS_MAX 64

struct fake {
	int script[CALLS_MAX];  /* Ergebnis je Anfrage; nach dem Ende METER_BUS_OK */
	int n_script;
	int calls;
	struct {
		uint8_t unit, fc;
		uint16_t reg, n;
	} call[CALLS_MAX];
	uint32_t sleep_before[CALLS_MAX]; /* Pause vor der Anfrage */
	uint32_t slept;
	int64_t now;
	uint8_t type;           /* Registerbild aus meter_sim_encode */
	int64_t v[METER_Q_COUNT];
	/* Bild von Hand: wenn gesetzt, statt meter_sim_encode */
	const uint16_t *raw[3];
	uint16_t raw_reg[3];
};

static int f_read(void *ctx, uint8_t unit, uint8_t fc, uint16_t reg, uint16_t n, uint16_t *out)
{
	struct fake *f = ctx;
	int i = f->calls++;

	zassert_true(i < CALLS_MAX);
	f->call[i].unit = unit;
	f->call[i].fc = fc;
	f->call[i].reg = reg;
	f->call[i].n = n;
	f->sleep_before[i] = f->slept;
	f->slept = 0;
	int r = i < f->n_script ? f->script[i] : METER_BUS_OK;

	if (r != METER_BUS_OK) {
		f->now += r == METER_BUS_TIMEOUT ? METER_RESPONSE_MS : 20;
		return r;
	}
	f->now += 20;
	for (int b = 0; b < 3; b++) {
		if (f->raw[b] != NULL && f->raw_reg[b] == reg) {
			memcpy(out, f->raw[b], n * sizeof(uint16_t));
			return METER_BUS_OK;
		}
	}
	return meter_sim_encode(f->type, fc, reg, n, f->v, out);
}

static void f_sleep(void *ctx, uint32_t ms)
{
	struct fake *f = ctx;

	f->slept += ms;
	f->now += ms;
}

static int64_t f_now(void *ctx)
{
	return ((struct fake *)ctx)->now;
}

static struct fake fk;
static struct meter m;

static void setup(uint8_t type)
{
	static const struct meter_bus bus = {f_read, f_sleep, f_now, &fk};
	static const int64_t v[METER_Q_COUNT] = {
		230000, 231500, 229800, 10500, 0, 12250, 2415250, 0, 1500000,
		3915000, 50000, 1234500, 0};

	memset(&fk, 0, sizeof(fk));
	fk.now = 1000;
	fk.type = type;
	memcpy(fk.v, v, sizeof(v));
	meter_init(&m, type, 7, &bus);
}

static void values(struct meter_values *mv)
{
	meter_values(&m, fk.now, mv);
}

ZTEST(meter, test_none)
{
	struct meter_values mv;
	char s[64];
	struct wf_jbuf b;

	setup(METER_NONE);
	zassert_equal(m.status, METER_OFF);
	zassert_equal(meter_cycle(&m), METER_PERIOD_ERROR_MS);
	zassert_equal(fk.calls, 0, "ohne Zaehler keine Anfrage (SOLL-MTR-01)");
	meter_init(&m, 99, 1, NULL);
	zassert_equal(m.type, METER_NONE);
	values(&mv);
	wf_jb_init(&b, s, sizeof(s));
	wf_meter_values_json(&mv, &b);
	zassert_str_equal(s, "[]");
	wf_jb_init(&b, s, sizeof(s));
	wf_meter_value_ids_json(METER_NONE, &b);
	zassert_str_equal(s, "[]");
}

/* SOLL-MTR-06: Bloecke, Funktionscodes und Adresse je Modell */
ZTEST(meter, test_requests_per_model)
{
	setup(METER_ABB_B23);
	zassert_equal(meter_cycle(&m), METER_PERIOD_MS);
	zassert_equal(fk.calls, 3);
	zassert_equal(fk.call[0].fc, 3);
	zassert_equal(fk.call[0].unit, 7);
	zassert_equal(fk.call[0].reg, 0x5B00);
	zassert_equal(fk.call[0].n, 28);
	zassert_equal(fk.call[1].reg, 0x5000);
	zassert_equal(fk.call[1].n, 8);
	zassert_equal(fk.call[2].reg, 0x5B2C);
	zassert_equal(fk.call[2].n, 1);

	setup(METER_SDM630);
	meter_cycle(&m);
	zassert_equal(fk.calls, 2);
	zassert_equal(fk.call[0].fc, 4);
	zassert_equal(fk.call[0].reg, 0x0000);
	zassert_equal(fk.call[0].n, 18);
	zassert_equal(fk.call[1].reg, 0x0034);
	zassert_equal(fk.call[1].n, 24);

	setup(METER_SDM72_V1);
	meter_cycle(&m);
	zassert_equal(fk.calls, 1);
	zassert_equal(fk.call[0].reg, 0x0034);
	zassert_equal(meter_regs_total(METER_SDM72_V1), 24);
	zassert_equal(meter_regs_total(METER_ABB_B23), 37);
	/* SOLL-MTR-10: Ruhepause vor jeder Anfrage */
	zassert_equal(fk.sleep_before[0], METER_GAP_MS);
}

/* ABB: Ganzzahlen mit Skalierung, Registerwerte von Hand (Handbuch B-Serie) */
ZTEST(meter, test_abb_decode)
{
	static uint16_t r0[28], r1[8], r2[1];
	struct meter_values mv;

	setup(METER_ABB_B23);
	memset(r0, 0, sizeof(r0));
	r0[1] = 2301;                   /* U1 230,1 V */
	r0[3] = 2315;
	r0[5] = 2298;
	r0[13] = 1050;                  /* I1 10,50 A */
	r0[15] = 0;
	r0[17] = 1;                     /* I3 0,01 A */
	r0[20] = 0xFFFF;                /* P gesamt -123,45 W (S32) */
	r0[21] = 0xCFC7;
	r0[22] = 0x0003;                /* P1 = 0x0003AF75 = 241525 -> 2415,25 W */
	r0[23] = 0xAF75;
	memset(r1, 0, sizeof(r1));
	r1[2] = 0x0001;                 /* Bezug 0x0001E240 = 123456 -> 1234,56 kWh */
	r1[3] = 0xE240;
	r2[0] = 5000;                   /* 50,00 Hz */
	fk.raw[0] = r0;
	fk.raw_reg[0] = 0x5B00;
	fk.raw[1] = r1;
	fk.raw_reg[1] = 0x5000;
	fk.raw[2] = r2;
	fk.raw_reg[2] = 0x5B2C;
	meter_cycle(&m);
	values(&mv);
	zassert_equal(mv.status, METER_OK);
	zassert_equal(mv.v[METER_U1], 230100);
	zassert_equal(mv.v[METER_U2], 231500);
	zassert_equal(mv.v[METER_I1], 10500);
	zassert_equal(mv.v[METER_I3], 10);
	zassert_equal(mv.v[METER_P], -123450);
	zassert_equal(mv.v[METER_P1], 2415250);
	zassert_equal(mv.v[METER_E_IMP], 1234560);
	zassert_equal(mv.v[METER_E_EXP], 0);
	zassert_equal(mv.v[METER_F], 50000);
	zassert_true(mv.valid[METER_P3]);

	/* F39: Ungueltig-Kennung leert nur diesen Wert, der Satz gilt (SOLL-MTR-13) */
	/* U32 (alle Bytes FF) */
	r0[0] = 0xFFFF;
	r0[1] = 0xFFFF;
	meter_cycle(&m);
	values(&mv);
	zassert_equal(m.st.implausible, 0);
	zassert_equal(m.st.sets, 2);
	zassert_false(mv.valid[METER_U1]);
	zassert_true(mv.valid[METER_U2]);
	/* S32: Hoechstwert = ungueltig */
	r0[1] = 2301;
	r0[0] = 0;
	r0[20] = 0x7FFF;
	r0[21] = 0xFFFF;
	meter_cycle(&m);
	values(&mv);
	zassert_true(mv.valid[METER_U1]);
	zassert_false(mv.valid[METER_P]);
	/* U16 FFFF */
	r0[20] = 0;
	r0[21] = 0;
	r2[0] = 0xFFFF;
	meter_cycle(&m);
	values(&mv);
	zassert_false(mv.valid[METER_F]);
	zassert_true(mv.valid[METER_P]);
	/* U64 alle FF: wie ABB B23 ohne Einspeisezaehlung (Testgeraet 2026-11-14), Bezug bleibt */
	r2[0] = 5000;
	memset(r1 + 4, 0xFF, 8);
	meter_cycle(&m);
	values(&mv);
	zassert_equal(mv.status, METER_OK);
	zassert_true(mv.valid[METER_E_IMP]);
	zassert_false(mv.valid[METER_E_EXP]);
	zassert_equal(m.st.implausible, 0);
	zassert_equal(m.st.sets, 5);
	/* ausserhalb des Bereichs bleibt unplausibel: kein Satz */
	r0[0] = 0;
	r0[1] = 9000;                   /* 900 V */
	meter_cycle(&m);
	zassert_equal(m.st.implausible, 1);
	zassert_equal(m.st.sets, 5);
}

static void put_f(uint16_t *r, uint32_t w)
{
	r[0] = (uint16_t)(w >> 16);
	r[1] = (uint16_t)w;
}

/* Eastron: Float32, Worte von Hand (IEEE 754) */
ZTEST(meter, test_sdm_decode)
{
	static uint16_t r0[18], r1[24];
	struct meter_values mv;

	setup(METER_SDM72_V2);
	memset(r0, 0, sizeof(r0));
	memset(r1, 0, sizeof(r1));
	put_f(&r0[0], 0x43660000);  /* 230,0 V */
	put_f(&r0[2], 0x43678000);  /* 231,5 V */
	put_f(&r0[4], 0x4365cccd);  /* 229,8 V */
	put_f(&r0[6], 0x41280000);  /* 10,5 A */
	put_f(&r0[12], 0x4516f400); /* 2415,25 W */
	put_f(&r0[16], 0xc3160000); /* -150 W (Einspeisung) */
	put_f(&r1[0], 0x4516f400);  /* gesamt */
	put_f(&r1[18], 0x42480000); /* 50 Hz an 0x0046 */
	put_f(&r1[20], 0x449a5000); /* 1234,5 kWh an 0x0048 */
	put_f(&r1[22], 0x41440000); /* 12,25 kWh an 0x004A */
	fk.raw[0] = r0;
	fk.raw_reg[0] = 0x0000;
	fk.raw[1] = r1;
	fk.raw_reg[1] = 0x0034;
	meter_cycle(&m);
	values(&mv);
	zassert_equal(mv.status, METER_OK);
	zassert_equal(mv.v[METER_U1], 230000);
	zassert_equal(mv.v[METER_U2], 231500);
	zassert_equal(mv.v[METER_U3], 229800);
	zassert_equal(mv.v[METER_I1], 10500);
	zassert_equal(mv.v[METER_P1], 2415250);
	zassert_equal(mv.v[METER_P3], -150000);
	zassert_equal(mv.v[METER_P], 2415250);
	zassert_equal(mv.v[METER_F], 50000);
	zassert_equal(mv.v[METER_E_IMP], 1234500);
	zassert_equal(mv.v[METER_E_EXP], 12250);

	/* NaN -> kein Satz */
	put_f(&r0[6], 0x7fc00000);
	meter_cycle(&m);
	zassert_equal(m.st.implausible, 1);
	/* unendlich */
	put_f(&r0[6], 0x7f800000);
	meter_cycle(&m);
	zassert_equal(m.st.implausible, 2);
	zassert_equal(m.st.sets, 1);
}

/* SOLL-MTR-13: Grenzen */
ZTEST(meter, test_limits)
{
	static uint16_t r0[18], r1[24];

	setup(METER_SDM630);
	memset(r0, 0, sizeof(r0));
	memset(r1, 0, sizeof(r1));
	put_f(&r1[18], 0x42480000); /* 50 Hz */
	fk.raw[0] = r0;
	fk.raw_reg[0] = 0x0000;
	fk.raw[1] = r1;
	fk.raw_reg[1] = 0x0034;
	put_f(&r0[0], 0x43960000);  /* 300,0 V: gerade noch */
	meter_cycle(&m);
	zassert_equal(m.st.sets, 1);
	put_f(&r0[0], 0x43960ccd);  /* 300,1 V */
	meter_cycle(&m);
	zassert_equal(m.st.sets, 1);
	put_f(&r0[0], 0);
	put_f(&r1[18], 0x4233999a); /* 44,9 Hz */
	meter_cycle(&m);
	zassert_equal(m.st.sets, 1);
	zassert_equal(m.st.implausible, 2);
	/* negativer Strom */
	put_f(&r1[18], 0x42480000);
	put_f(&r0[6], 0xc3160000);
	meter_cycle(&m);
	zassert_equal(m.st.implausible, 3);
}

/* SOLL-MTR-11: Wiederholung, Zusatzpause nach Timeout, Zaehler je Art */
ZTEST(meter, test_retry_and_counters)
{
	setup(METER_SDM630);
	fk.script[0] = METER_BUS_TIMEOUT;
	fk.script[1] = METER_BUS_FRAME;
	fk.script[2] = METER_BUS_OK;     /* Block 1 im dritten Versuch */
	fk.script[3] = 2;                /* Block 2: Ausnahme Illegal data address */
	fk.n_script = 4;
	meter_cycle(&m);
	zassert_equal(fk.calls, 5);
	zassert_equal(fk.sleep_before[1], METER_GAP_MS + METER_RESPONSE_MS,
		      "nach Timeout Antwortfrist zusaetzlich abwarten");
	zassert_equal(fk.sleep_before[2], METER_GAP_MS, "nach Rahmenfehler nur Ruhepause");
	zassert_equal(m.st.requests, 5);
	zassert_equal(m.st.responses, 2);
	zassert_equal(m.st.timeouts, 1);
	zassert_equal(m.st.frame_errors, 1);
	zassert_equal(m.st.exceptions, 1);
	zassert_equal(m.st.last_exception, 2);
	zassert_equal(m.st.sets, 1);

	fk.calls = 0;
	fk.script[0] = 2;
	fk.script[1] = 2;
	fk.script[2] = 4;
	fk.n_script = 3;
	meter_cycle(&m);
	zassert_equal(fk.calls, 3, "hoechstens 3 Versuche je Block, dann Zyklusende");
	zassert_equal(m.st.exceptions, 4);
	zassert_equal(m.st.last_exception, 4);
	zassert_equal(m.st.sets, 1);
}

/* SOLL-MTR-12: Teilerfolg ergibt keinen Satz; alter Satz bleibt mit Alter */
ZTEST(meter, test_partial_cycle)
{
	struct meter_values mv;

	setup(METER_SDM630);
	meter_cycle(&m);
	int64_t t_ok = fk.now;

	fk.v[METER_U1] = 240000;
	fk.calls = 0;
	fk.script[0] = METER_BUS_OK;
	fk.script[1] = METER_BUS_TIMEOUT;
	fk.script[2] = METER_BUS_TIMEOUT;
	fk.script[3] = METER_BUS_TIMEOUT;
	fk.n_script = 4;
	meter_cycle(&m);
	values(&mv);
	zassert_equal(mv.v[METER_U1], 230000, "Block 1 dieses Zyklus darf nicht wirken");
	zassert_true(mv.valid[METER_U1]);
	zassert_equal(mv.age_ms, fk.now - t_ok);
}

/* SOLL-MTR-12/14: Zustandsfolge, veraltete Werte, Abfrageabstand */
ZTEST(meter, test_status_and_stale)
{
	struct meter_values mv;

	setup(METER_ABB_B23);
	zassert_equal(m.status, METER_CONNECTING);
	values(&mv);
	zassert_equal(mv.age_ms, -1);
	zassert_false(mv.valid[METER_P]);

	meter_cycle(&m);
	zassert_equal(m.status, METER_OK);
	zassert_equal(meter_take_events(&m), METER_EV_STATUS);
	zassert_equal(meter_take_events(&m), 0);

	fk.n_script = CALLS_MAX;
	for (int i = 0; i < CALLS_MAX; i++) {
		fk.script[i] = METER_BUS_TIMEOUT;
	}
	fk.calls = 0;
	zassert_equal(meter_cycle(&m), METER_PERIOD_MS);
	zassert_equal(meter_cycle(&m), METER_PERIOD_MS);
	zassert_equal(m.status, METER_OK, "2 Fehlzyklen: noch ok");
	zassert_equal(meter_cycle(&m), METER_PERIOD_ERROR_MS);
	zassert_equal(m.status, METER_ERROR);
	zassert_equal(meter_take_events(&m), METER_EV_STATUS);

	/* Thread wartet danach METER_PERIOD_ERROR_MS */
	meter_values(&m, fk.now + METER_PERIOD_ERROR_MS, &mv);
	zassert_true(mv.age_ms > METER_STALE_MS);
	zassert_false(mv.valid[METER_U1], "nach 10 s ungueltig");
	/* kurz nach dem letzten Satz noch gueltig */
	meter_values(&m, m.t_set + METER_STALE_MS, &mv);
	zassert_true(mv.valid[METER_U1]);
	meter_values(&m, m.t_set + METER_STALE_MS + 1, &mv);
	zassert_false(mv.valid[METER_U1]);

	fk.n_script = 0;
	fk.calls = 0;
	zassert_equal(meter_cycle(&m), METER_PERIOD_MS);
	zassert_equal(m.status, METER_OK);
	zassert_equal(meter_take_events(&m), METER_EV_STATUS);

	/* aus "connecting" direkt nach error */
	setup(METER_SDM630);
	fk.n_script = CALLS_MAX;
	for (int i = 0; i < CALLS_MAX; i++) {
		fk.script[i] = METER_BUS_FRAME;
	}
	meter_cycle(&m);
	meter_cycle(&m);
	zassert_equal(m.status, METER_CONNECTING);
	meter_cycle(&m);
	zassert_equal(m.status, METER_ERROR);
}

/* SOLL-MTR-13: Zaehlerstand sinkt -> uebernehmen und melden */
ZTEST(meter, test_energy_down)
{
	struct meter_values mv;

	setup(METER_SDM630);
	meter_cycle(&m);
	(void)meter_take_events(&m);
	fk.v[METER_E_IMP] = 1234600;
	meter_cycle(&m);
	zassert_equal(meter_take_events(&m), 0);
	fk.v[METER_E_IMP] = 5000;
	meter_cycle(&m);
	zassert_equal(meter_take_events(&m), METER_EV_ENERGY_DOWN);
	values(&mv);
	zassert_equal(mv.v[METER_E_IMP], 5000);
}

/* Rundlauf Simulation -> Dekodierung fuer alle Modelle */
ZTEST(meter, test_sim_roundtrip)
{
	for (uint8_t t = METER_ABB_B23; t < METER_TYPE_COUNT; t++) {
		struct meter_values mv;

		setup(t);
		meter_cycle(&m);
		values(&mv);
		zassert_equal(mv.status, METER_OK, "Typ %u", t);
		for (int q = 0; q < METER_Q_COUNT; q++) {
			zassert_equal(mv.valid[q], meter_has(t, (enum meter_q)q));
			if (mv.valid[q]) {
				zassert_equal(mv.v[q], fk.v[q], "Typ %u Groesse %d", t, q);
			}
		}
	}
	uint16_t r[4];

	zassert_equal(meter_sim_encode(METER_SDM630, 3, 0x0000, 18, fk.v, r), 2,
		      "falscher Funktionscode -> Ausnahme 2");
}

ZTEST(meter, test_json)
{
	char s[512];
	struct wf_jbuf b;
	struct meter_values mv;

	wf_jb_init(&b, s, sizeof(s));
	wf_jb_milli(&b, 0);
	wf_jb_raw(&b, " ");
	wf_jb_milli(&b, -1);
	wf_jb_raw(&b, " ");
	wf_jb_milli(&b, 1234567);
	wf_jb_raw(&b, " ");
	wf_jb_milli(&b, -75000000);
	zassert_str_equal(s, "0.000 -0.001 1234.567 -75000.000");

	wf_jb_init(&b, s, sizeof(s));
	wf_meter_value_ids_json(METER_ABB_B23, &b);
	zassert_str_equal(s, "[1,2,3,13,17,21,39,48,57,74,364,209,211]");
	wf_jb_init(&b, s, sizeof(s));
	wf_meter_value_ids_json(METER_SDM72_V1, &b);
	zassert_str_equal(s, "[74,209,211]");

	setup(METER_SDM72_V1);
	values(&mv);
	wf_jb_init(&b, s, sizeof(s));
	wf_meter_values_json(&mv, &b);
	zassert_str_equal(s, "[null,null,null]", "ungueltig = null");
	meter_cycle(&m);
	values(&mv);
	wf_jb_init(&b, s, sizeof(s));
	wf_meter_values_json(&mv, &b);
	zassert_str_equal(s, "[3915.000,1234.500,0.000]");

	wf_jb_init(&b, s, sizeof(s));
	wf_meter_state_json(&mv, &b);
	zassert_not_null(strstr(s, "\"state\":2,\"type\":4,\"status\":\"ok\""), "%s", s);
	zassert_not_null(strstr(s, "\"requests\":1,\"responses\":1,\"timeouts\":0"), "%s", s);
	zassert_not_null(strstr(s, "\"sets\":1,\"last_exception\":0}"), "%s", s);
	zassert_false(b.overflow);
}

ZTEST_SUITE(meter, NULL, NULL, NULL, NULL, NULL);
