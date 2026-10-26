/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Simulierte Hardwareschicht fuer native_sim (Ende-zu-Ende-Test auf dem PC).
 * Ersetzt hw_stm32.c und hw_gpio.c; die uebrige Anwendung (Threads,
 * Speicher, HTTP, Shell) laeuft unveraendert.
 *
 *  - Messbloecke alle 8 ms aus einem Modell von Fahrzeug, Kabel, Eingaengen,
 *    Bender-Sensor (Trip-Test), Verriegelungsaktor (LD) und Schuetz-Sense.
 *    Die Pegel entsprechen dem Szenario-Modell der Ladelogik-Tests
 *    (tests/evse/charger/src/sim.c) in der Skalierung des Originals.
 *  - Ein RCM-Signal loest den Schutz-Latch sofort aus (wie der Interrupt).
 *  - IWDG als Software-Zeitgeber in simulierter Zeit.
 *  - Reset: Prozess startet sich neu (sim_host.c); Flags und Backup-Code
 *    gehen ueber die Umgebungsvariable WALLFREE_SIM_RESET mit, der
 *    physische Modellzustand (Fahrzeug, Aktorstellung ...) ueber
 *    WALLFREE_SIM_MODEL. Ein neuer Prozess entspricht dem Einschalten.
 *  - DIP, Drehschalter und Reset-Taster ueber Kommandozeilenoptionen,
 *    das Modell zur Laufzeit ueber die Shell (Kommando "sim").
 *  - Energiezaehler hinter meter_bus.h (SOLL-MTR-04): Antworten aus
 *    meter_sim_encode(); Strom = Strom des Tastgrads bei Schuetz ein und
 *    Fahrzeug in C/D, sonst 0; Stoerungen ueber "sim meter".
 *
 * Rein fuer Tests auf dem PC; keine Aussage ueber reale Hardware.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/arch/posix/posix_trace.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <cmdline.h>
#include <nsi_host_trampolines.h>
#include <posix_native_task.h>
#include <wallfree/meter/meter.h>
#include "hw.h"
#include "meter_bus.h"
#include "sim_host.h"

LOG_MODULE_REGISTER(hw_sim, LOG_LEVEL_INF);

#define BLOCK_MS      8     /* 8 PWM-Perioden */
#define LEVEL_ON_MV   12000
#define RESET_ENV     "WALLFREE_SIM_RESET"
#define MODEL_ENV     "WALLFREE_SIM_MODEL" /* physischer Zustand ueber Neustarts */
#define IWDG_LONG_MS  32000 /* wie hw_iwdg_long auf der Hardware (~32 s) */

K_SEM_DEFINE(hw_block_sem, 0, 1);

/* ------------------------------------------------ Kommandozeile */

/* nicht angegebene Optionen setzt der Simulator auf UINT32_MAX */
static uint32_t opt_dip;
static uint32_t opt_rotary;
static uint32_t opt_reset_ms;
static uint32_t opt_http_port;
static bool opt_image_bad; /* SOLL-BOOT-35 pruefen: Abbild als ungueltig melden */

static void sim_options(void)
{
	static struct args_struct_t opts[] = {
		{.option = "dip", .name = "mask", .type = 'u', .dest = &opt_dip,
		 .descript = "DIP-Schalter D1..D10 als Bitmaske (Bit 0 = D1), Default 0"},
		{.option = "rotary", .name = "pos", .type = 'u', .dest = &opt_rotary,
		 .descript = "Drehschalter 0..15, Default 6 (32 A)"},
		{.option = "reset-button", .name = "ms", .type = 'u', .dest = &opt_reset_ms,
		 .descript = "Reset-Taster beim Start so lange gedrueckt halten (5000 = Werksreset)"},
		{.option = "http-port", .name = "port", .type = 'u', .dest = &opt_http_port,
		 .descript = "TCP-Port des HTTP-Servers, Default CONFIG_WALLFREE_HTTP_PORT"},
		{.is_switch = true, .option = "image-bad", .type = 'b', .dest = &opt_image_bad,
		 .descript = "Pruefung des eigenen Abbilds schlaegt fehl (SOLL-BOOT-35)"},
		ARG_TABLE_ENDMARKER,
	};

	native_add_command_line_opts(opts);
}
NATIVE_TASK(sim_options, PRE_BOOT_1, 10);

static void sim_option_defaults(void)
{
	opt_dip = opt_dip == UINT32_MAX ? 0 : opt_dip;
	opt_rotary = opt_rotary == UINT32_MAX ? 6 : opt_rotary; /* 32 A */
	opt_reset_ms = opt_reset_ms == UINT32_MAX ? 0 : opt_reset_ms;
	opt_http_port = opt_http_port == UINT32_MAX ? CONFIG_WALLFREE_HTTP_PORT : opt_http_port;
}
NATIVE_TASK(sim_option_defaults, PRE_BOOT_2, 10);

uint16_t hw_sim_http_port(void)
{
	return (uint16_t)opt_http_port;
}

/* ------------------------------------------------ Modellzustand */

enum ld_model {
	LD_NONE = 0,
	LD_PERMANENT, /* Kontakt geschlossen, solange verriegelt */
	LD_PULSE,     /* nur waehrend der Fahrt (Kuester) */
};

static struct {
	/* Fahrzeug und Kabel */
	enum evse_cp_state car;
	bool diode;
	int32_t pp_mv;
	int32_t din_mv[EVSE_DIN_COUNT];
	int32_t supply_mv;
	/* Bender */
	bool rcm_healthy;
	bool rcm_fault;
	bool rcm_err;
	int64_t test_at;
	bool sw1;
	bool sw2;
	bool rcm_prev;
	/* Aktor */
	enum ld_model ld;
	bool locked;
	bool stuck;
	int64_t pulse_start;
	bool pulse_was;
	/* Schuetz */
	bool welded;
	int64_t last_edge;
	/* Ausgaenge (logisch, wie hw_gpio_apply sie setzt) */
	uint16_t duty;
	bool contactor;
	bool lock_pin;
	bool unlock_pin;
	bool rcm_test;
	bool pe1;
	bool dout[EVSE_DOUT_COUNT];
	bool led[EVSE_LED_COUNT];
	bool dout_en;
	bool net_led;
	bool link;
	/* Messung */
	struct hw_block block;
	bool block_valid;
	uint32_t ms;
} m = {
	.car = EVSE_CP_A,
	.diode = true,
	.pp_mv = 9000,
	.supply_mv = 12000,
	.rcm_healthy = true,
	.test_at = -1,
	.ld = LD_PULSE,
	.pulse_start = -1,
	.last_edge = INT64_MIN,
	.link = true,
};

static atomic_t protect_latch;

static int32_t cp_level(enum evse_cp_state st)
{
	static const int32_t mv[] = {12000, 9000, 6000, 3000};

	return st <= EVSE_CP_D ? mv[st] : 0; /* E: Kurzschluss */
}

/* ------------------------------------------------ Modelltakt (1 ms, Zeitgeber-ISR) */

static void tick(struct k_timer *t)
{
	ARG_UNUSED(t);
	int64_t now = k_uptime_get();
	bool give = false;
	unsigned int key = irq_lock();

	/* Bender: SWITCH_2 330 ms, SWITCH_1 770 ms nach Testbeginn, Ende 1,9 s */
	if (m.rcm_test && m.test_at < 0) {
		m.test_at = now;
	}
	bool sw1 = false, sw2 = false;

	if (m.test_at >= 0) {
		int64_t dt = now - m.test_at;

		if (m.rcm_healthy) {
			sw2 = dt >= 330 && dt < 1900;
			sw1 = dt >= 770 && dt < 1900;
		}
		if (dt >= 2500) {
			m.test_at = -1;
		}
	}
	if (m.rcm_fault) {
		sw1 = sw2 = true;
	}
	bool rcm = sw1 || sw2 || m.rcm_err;

	m.sw1 = sw1;
	m.sw2 = sw2;

	/* Flanke nach "ausgeloest" -> Schutz-Latch wie der GPIO-Interrupt */
	if (rcm && !m.rcm_prev) {
		m.contactor = false;
		atomic_set(&protect_latch, 1);
	}
	m.rcm_prev = rcm;

	/* Aktor: Fahrt 200 ms nach Impulsbeginn, danach Endlage */
	bool pulse = m.lock_pin || m.unlock_pin;

	if (pulse && !m.pulse_was) {
		m.pulse_start = now;
	}
	bool moving = pulse && !m.stuck && now - m.pulse_start < 200;

	if (pulse && !m.stuck && now - m.pulse_start >= 200) {
		m.locked = m.lock_pin;
	}
	m.pulse_was = pulse;

	bool ld = m.ld == LD_PULSE ? moving : m.ld == LD_PERMANENT ? m.locked : false;

	/* Schuetz-Sense: 50 Hz hinter dem Schuetz */
	if ((m.contactor || m.welded) && now % 20 == 0) {
		m.last_edge = now;
	}

	if (++m.ms % BLOCK_MS == 0) {
		m.block.cp_pos = cp_level(m.car);
		m.block.cp_neg = (m.car == EVSE_CP_A || m.diode) ? 12000 : m.block.cp_pos;
		m.block.pp = m.pp_mv;
		for (int i = 0; i < EVSE_DIN_COUNT; i++) {
			m.block.din[i] = m.din_mv[i];
		}
		m.block.din[EVSE_DIN_LD] = ld ? LEVEL_ON_MV : 0;
		m.block.supply = m.supply_mv;
		m.block.temp_raw = 0;
		m.block.seq++;
		m.block.at_ms = now;
		m.block_valid = true;
		give = true;
	}
	irq_unlock(key);
	if (give) {
		k_sem_give(&hw_block_sem);
	}
}
static K_TIMER_DEFINE(model_timer, tick, NULL);

/* ------------------------------------------------ Analogteil (hw_stm32.c) */

int hw_analog_init(void)
{
	k_timer_start(&model_timer, K_MSEC(1), K_MSEC(1));
	LOG_INF("Simulation: CP-Modell und Messbloecke gestartet");
	return 0;
}

void hw_cp_set_duty(uint16_t permille)
{
	m.duty = permille > 1000 ? 1000 : permille;
}

bool hw_get_block(struct hw_block *out)
{
	unsigned int key = irq_lock();
	bool ok = m.block_valid;

	*out = m.block;
	irq_unlock(key);
	return ok;
}

void hw_contactor_off_regs(void)
{
	m.contactor = false;
}

void hw_safe_state_regs(void)
{
	unsigned int key = irq_lock();

	m.contactor = false;
	m.duty = 0;
	m.lock_pin = false;
	m.unlock_pin = false;
	m.rcm_test = false;
	irq_unlock(key);
}

/* ------------------------------------------------ Modell ueber Neustarts */

/* Was ein Reset der Steuerung nicht aendert: Fahrzeug, Kabel, Eingaenge,
 * Sensor, Aktorstellung, Schuetzkontakt */
static void model_save(void)
{
	char v[128];

	snprintf(v, sizeof(v), "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", m.car, m.diode,
		 m.pp_mv, m.din_mv[EVSE_DIN_EN], m.din_mv[EVSE_DIN_ML], m.din_mv[EVSE_DIN_XR],
		 m.din_mv[EVSE_DIN_IN], m.supply_mv, m.rcm_healthy, m.rcm_fault, m.rcm_err, m.ld,
		 m.locked, m.stuck, m.welded);
	(void)nsi_host_setenv(MODEL_ENV, v, 1);
}

static void model_restore(void)
{
	const char *v = nsi_host_getenv(MODEL_ENV);
	int f[15];

	if (v == NULL || sscanf(v, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &f[0], &f[1],
				&f[2], &f[3], &f[4], &f[5], &f[6], &f[7], &f[8], &f[9], &f[10],
				&f[11], &f[12], &f[13], &f[14]) != 15) {
		return;
	}
	m.car = (enum evse_cp_state)f[0];
	m.diode = f[1];
	m.pp_mv = f[2];
	m.din_mv[EVSE_DIN_EN] = f[3];
	m.din_mv[EVSE_DIN_ML] = f[4];
	m.din_mv[EVSE_DIN_XR] = f[5];
	m.din_mv[EVSE_DIN_IN] = f[6];
	m.supply_mv = f[7];
	m.rcm_healthy = f[8];
	m.rcm_fault = f[9];
	m.rcm_err = f[10];
	m.ld = (enum ld_model)f[11];
	m.locked = f[12];
	m.stuck = f[13];
	m.welded = f[14];
	LOG_INF("Simulation: Modell nach Neustart uebernommen (Fahrzeug %c)", 'A' + m.car);
}

static FUNC_NORETURN void restart(const char *reset_env)
{
	(void)nsi_host_setenv(RESET_ENV, reset_env, 1);
	model_save();
	wf_sim_host_reset_now();
	CODE_UNREACHABLE;
}

/* ------------------------------------------------ IWDG */

static uint32_t wdt_ms;
static uint32_t wdt_saved_ms;

static void wdt_expired(struct k_timer *t)
{
	ARG_UNUSED(t);
	hw_safe_state_regs();
	/* direkt auf den Host: printk laeuft ueber den Log-Puffer und ginge verloren */
	posix_print_warning("SIM: IWDG abgelaufen (%u ms), Reset\n", wdt_ms);
	char v[32];

	snprintf(v, sizeof(v), "%x:0", WF_RSTF_IWDG);
	restart(v);
}
static K_TIMER_DEFINE(wdt_timer, wdt_expired, NULL);

int hw_wdt_start(uint32_t timeout_ms)
{
	wdt_ms = timeout_ms;
	k_timer_start(&wdt_timer, K_MSEC(wdt_ms), K_NO_WAIT);
	return 0;
}

void hw_wdt_feed(void)
{
	if (wdt_ms != 0) {
		k_timer_start(&wdt_timer, K_MSEC(wdt_ms), K_NO_WAIT);
	}
}

void hw_iwdg_long(bool on)
{
	if (on) {
		wdt_saved_ms = wdt_ms;
		wdt_ms = IWDG_LONG_MS;
	} else {
		wdt_ms = wdt_saved_ms;
	}
	hw_wdt_feed();
}

/* ------------------------------------------------ Flash */

uint32_t hw_flash_take_errors(void)
{
	return 0;
}

uint32_t hw_flash_errors_early(void)
{
	return 0;
}

/* ------------------------------------------------ Resetgrund */

void hw_reset_take(uint32_t *flags, uint32_t *bkp)
{
	const char *v = nsi_host_getenv(RESET_ENV);

	*flags = WF_RSTF_POR | WF_RSTF_PIN; /* Prozessstart = Einschalten */
	*bkp = 0;
	if (v != NULL && v[0] != '\0') {
		char *end;

		*flags = (uint32_t)strtoul(v, &end, 16);
		if (*end == ':') {
			*bkp = (uint32_t)strtoul(end + 1, NULL, 16);
		}
	}
	(void)nsi_host_setenv(RESET_ENV, "", 1);
}

void hw_fault_note(unsigned int reason, uint32_t pc)
{
	posix_print_warning("SIM: Fehler %u bei pc 0x%08x\n", reason, pc);
}

/*
 * Bootloader: Ist WALLFREE_SIM_BOOTLOADER gesetzt (Pfad zu scripts/bootloader-sim.py),
 * uebernimmt dieses Skript den HTTP-Port wie der Update-Betrieb des Original-
 * Bootloaders und startet danach die Simulation mit dem Ergebnis in BKP12 neu.
 * Sonst gilt das Update sofort als fehlgeschlagen (BKP12 = Modus << 8).
 */
#define BOOTLOADER_ENV "WALLFREE_SIM_BOOTLOADER"
#define BKP12_ENV      "WALLFREE_SIM_BKP12"

FUNC_NORETURN void hw_bootloader_handover(uint32_t ip, uint32_t mask)
{
	char v[32];
	const char *mock = nsi_host_getenv(BOOTLOADER_ENV);

	hw_safe_state_regs();
	snprintf(v, sizeof(v), "%x:%x", WF_RSTF_SFT | WF_RSTF_PIN,
		 wf_reset_bkp_value(WF_RST_UPDATE));
	(void)nsi_host_setenv(RESET_ENV, v, 1);
	posix_print_warning("SIM: Uebergabe an den Bootloader (IP 0x%08x, Maske 0x%08x)\n", ip,
			    mask);
	if (mock == NULL || mock[0] == '\0') {
		(void)nsi_host_setenv(BKP12_ENV, "200", 1);
		restart(v);
	}
	model_save();
	wf_sim_host_exec_bootloader(mock, hw_sim_http_port());
	CODE_UNREACHABLE;
}

/* SOLL-BOOT-35: Das Programm der Simulation hat keinen Anhang. Nach einem Update meldet der
 * Ersatz-Bootloader in WALLFREE_SIM_IMAGE, ob das empfangene Abbild gueltig war
 * ("ok:<crc>"/"bad:<crc>"); --image-bad erzwingt ein ungueltiges. Sonst nicht pruefbar. */
bool hw_device_mac(uint8_t mac[6])
{
	ARG_UNUSED(mac);
	return false; /* Host-Sockets, keine eigene Schnittstelle */
}

int hw_image_check(uint32_t *crc)
{
	const char *v = nsi_host_getenv("WALLFREE_SIM_IMAGE");

	*crc = 0;
	if (opt_image_bad) {
		return 0;
	}
	if (v == NULL || strlen(v) < 4) {
		return -1;
	}
	*crc = (uint32_t)strtoul(v + 3, NULL, 16);
	return strncmp(v, "ok:", 3) == 0;
}

void hw_boot_regs(uint32_t out[HW_BOOT_REGS])
{
	memset(out, 0, HW_BOOT_REGS * sizeof(out[0]));
}

uint32_t hw_update_result_take(void)
{
	const char *v = nsi_host_getenv(BKP12_ENV);
	uint32_t r = v != NULL ? (uint32_t)strtoul(v, NULL, 16) : 0;

	(void)nsi_host_setenv(BKP12_ENV, "", 1);
	return r;
}

FUNC_NORETURN void hw_reset_with(enum wf_reset code)
{
	char v[32];

	hw_safe_state_regs();
	snprintf(v, sizeof(v), "%x:%x", WF_RSTF_SFT | WF_RSTF_PIN, wf_reset_bkp_value(code));
	posix_print_warning("SIM: Reset (%s)\n", wf_reset_name(code));
	restart(v);
}

/* ------------------------------------------------ GPIO (hw_gpio.c) */

int hw_gpio_init(void)
{
	model_restore();
	LOG_INF("Simulation: DIP 0x%03x, Drehschalter %u%s", opt_dip, opt_rotary,
		opt_reset_ms ? ", Reset-Taster gedrueckt" : "");
	return 0;
}

void hw_gpio_read_switches(evse_dip_t *dip, uint8_t *rotary)
{
	*dip = (evse_dip_t)(opt_dip & EVSE_DIP_MASK);
	*rotary = (uint8_t)(opt_rotary & 0x0f);
}

bool hw_gpio_reset_button(void)
{
	return k_uptime_get() < (int64_t)opt_reset_ms;
}

void hw_gpio_net_led(bool on)
{
	m.net_led = on;
}

bool hw_sim_link(void)
{
	return m.link;
}

void hw_gpio_led(enum evse_led led, bool on)
{
	m.led[led] = on;
}

void hw_gpio_read_rcm(struct evse_rcm_levels *lv)
{
	unsigned int key = irq_lock();

	lv->sw1 = m.sw1;
	lv->sw2 = m.sw2;
	lv->err = m.rcm_err;
	irq_unlock(key);
}

int64_t hw_gpio_last_sense_edge(void)
{
	return m.last_edge;
}

bool hw_protect_latched(void)
{
	return atomic_get(&protect_latch) != 0;
}

void hw_protect_trip(void)
{
	hw_contactor_off_regs();
	atomic_set(&protect_latch, 1);
}

void hw_protect_clear(void)
{
	atomic_set(&protect_latch, 0);
}

void hw_gpio_apply(const struct evse_outputs *o)
{
	unsigned int key = irq_lock();

	m.contactor = o->contactor && !hw_protect_latched();
	m.pe1 = o->pe1;
	m.lock_pin = o->lock_pulse && !o->unlock_pulse;
	m.unlock_pin = o->unlock_pulse && !o->lock_pulse;
	m.rcm_test = o->rcm_test;
	memcpy(m.dout, o->dout, sizeof(m.dout));
	memcpy(m.led, o->led, sizeof(m.led));
	m.dout_en = true;
	irq_unlock(key);
}

/* ------------------------------------------------ Energiezaehler (meter_bus.h) */

enum meter_mode {
	MM_OK = 0,
	MM_OFF,    /* keine Antwort */
	MM_FRAME,  /* gestoerte Telegramme */
	MM_FLAKY,  /* jede zweite Anfrage ohne Antwort */
	MM_BAD,    /* unplausibler Wert (Spannung NaN bzw. ungueltig) */
};
static const char *const meter_modes[] = {"ok", "off", "frame", "flaky", "bad"};

static struct {
	uint8_t type;
	enum meter_mode mode;
	uint32_t n;
	int64_t energy_uj;   /* Bezug in Mikrojoule (mW x ms) */
	int64_t t_last;
} sm = {.energy_uj = 1234567LL * 3600000000LL}; /* 1234,567 kWh */

int meter_bus_init(const struct wf_sysconf *c)
{
	sm.type = c->meter_type;
	sm.t_last = k_uptime_get();
	return 0;
}

int meter_bus_read(void *ctx, uint8_t unit, uint8_t fc, uint16_t reg, uint16_t n,
		   uint16_t *out)
{
	ARG_UNUSED(ctx);
	ARG_UNUSED(unit);
	int64_t v[METER_Q_COUNT] = {0};
	int64_t now = k_uptime_get();
	bool flaky_miss = sm.mode == MM_FLAKY && (sm.n++ & 1);

	if (sm.mode == MM_OFF || flaky_miss) {
		k_msleep(METER_RESPONSE_MS); /* wie die Antwortfrist des Clients */
		return METER_BUS_TIMEOUT;
	}
	k_msleep(5 + n / 4); /* Uebertragungszeit grob */
	if (sm.mode == MM_FRAME) {
		return METER_BUS_FRAME;
	}
	unsigned int key = irq_lock();
	bool charging = m.contactor && (m.car == EVSE_CP_C || m.car == EVSE_CP_D);
	uint16_t duty = m.duty;

	irq_unlock(key);
	/* IEC 61851: 10..85 % -> 0,6 A je Prozent */
	int64_t ma = charging && duty >= 100 && duty <= 850 ? (int64_t)duty * 60 : 0;

	for (int i = 0; i < 3; i++) {
		v[METER_U1 + i] = 230000 + i * 700;
		v[METER_I1 + i] = ma;
		v[METER_P1 + i] = v[METER_U1 + i] * ma / 1000;
		v[METER_P] += v[METER_P1 + i];
	}
	v[METER_F] = 50000;
	sm.energy_uj += v[METER_P] * (now - sm.t_last);
	sm.t_last = now;
	v[METER_E_IMP] = sm.energy_uj / 3600000000LL; /* Wh = Tausendstel kWh */
	if (sm.mode == MM_BAD) {
		v[METER_U1] = 400000; /* ausserhalb der Grenze (SOLL-MTR-13) */
	}
	return meter_sim_encode(sm.type, fc, reg, n, v, out);
}

/* ------------------------------------------------ Shell "sim" */

static const char *const din_names[EVSE_DIN_COUNT] = {"ld", "en", "ml", "xr", "in"};
static const char *const ld_names[] = {"none", "permanent", "pulse"};

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	unsigned int key = irq_lock();
	__typeof__(m) c = m;

	irq_unlock(key);
	shell_print(sh, "car=%c diode=%d pp_mv=%d supply_mv=%d", 'A' + c.car, c.diode, c.pp_mv,
		    c.supply_mv);
	shell_print(sh, "din: en=%d ml=%d xr=%d in=%d (mV), ld=%s", c.din_mv[EVSE_DIN_EN],
		    c.din_mv[EVSE_DIN_ML], c.din_mv[EVSE_DIN_XR], c.din_mv[EVSE_DIN_IN],
		    ld_names[c.ld]);
	shell_print(sh, "rcm: healthy=%d fault=%d err=%d latch=%d", c.rcm_healthy, c.rcm_fault,
		    c.rcm_err, hw_protect_latched());
	shell_print(sh, "actuator: locked=%d stuck=%d  welded=%d", c.locked, c.stuck, c.welded);
	shell_print(sh, "out: duty=%u contactor=%d lock=%d unlock=%d rcm_test=%d pe1=%d", c.duty,
		    c.contactor, c.lock_pin, c.unlock_pin, c.rcm_test, c.pe1);
	shell_print(sh, "dout ER=%d LR=%d VR=%d CR=%d en=%d  led P=%d E=%d C=%d R=%d N=%d",
		    c.dout[0], c.dout[1], c.dout[2], c.dout[3], c.dout_en, c.led[0], c.led[1],
		    c.led[2], c.led[3], c.net_led);
	shell_print(sh, "link=%d", c.link);
	return 0;
}

static int parse_bool(const struct shell *sh, const char *s, bool *v)
{
	if (strcmp(s, "1") == 0 || strcmp(s, "on") == 0) {
		*v = true;
	} else if (strcmp(s, "0") == 0 || strcmp(s, "off") == 0) {
		*v = false;
	} else {
		shell_error(sh, "0/1 erwartet");
		return -EINVAL;
	}
	return 0;
}

static int cmd_car(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	char c = argv[1][0] & ~0x20;

	if (argv[1][1] != '\0' || c < 'A' || c > 'E') {
		shell_error(sh, "A..E erwartet (E = Kurzschluss)");
		return -EINVAL;
	}
	m.car = (enum evse_cp_state)(c - 'A');
	return 0;
}

static int cmd_diode(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return parse_bool(sh, argv[1], &m.diode);
}

static int cmd_cable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	static const struct {
		const char *name;
		int32_t mv;
	} t[] = {{"0", 9000}, {"13", 6000}, {"20", 4000}, {"32", 2000}, {"63", 1000},
		 {"invalid", 300}};

	for (size_t i = 0; i < ARRAY_SIZE(t); i++) {
		if (strcmp(argv[1], t[i].name) == 0) {
			m.pp_mv = t[i].mv;
			if ((opt_dip & BIT(0)) == 0) {
				/* SOLL-PP-01: ohne D1 gilt der Drehschalter */
				shell_warn(sh, "PP wird nur mit DIP D1 ausgewertet "
					       "(Start mit --dip=0x1)");
			}
			return 0;
		}
	}
	shell_error(sh, "0|13|20|32|63|invalid erwartet");
	return -EINVAL;
}

static int cmd_din(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	bool on;

	for (int i = EVSE_DIN_EN; i < EVSE_DIN_COUNT; i++) {
		if (strcmp(argv[1], din_names[i]) == 0) {
			if (parse_bool(sh, argv[2], &on) != 0) {
				return -EINVAL;
			}
			m.din_mv[i] = on ? LEVEL_ON_MV : 0;
			return 0;
		}
	}
	shell_error(sh, "en|ml|xr|in erwartet (LD kommt aus dem Aktormodell, siehe 'sim ld')");
	return -EINVAL;
}

static int cmd_ld(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	for (size_t i = 0; i < ARRAY_SIZE(ld_names); i++) {
		if (strcmp(argv[1], ld_names[i]) == 0) {
			m.ld = (enum ld_model)i;
			return 0;
		}
	}
	shell_error(sh, "none|permanent|pulse erwartet");
	return -EINVAL;
}

static int cmd_rcm(const struct shell *sh, size_t argc, char **argv)
{
	unsigned int key = irq_lock();
	int r = 0;

	if (strcmp(argv[1], "ok") == 0) {
		m.rcm_healthy = true;
		m.rcm_fault = false;
		m.rcm_err = false;
	} else if (strcmp(argv[1], "fault") == 0) {
		m.rcm_fault = true; /* Fehlerstrom: SWITCH_1 und SWITCH_2 */
	} else if (strcmp(argv[1], "clear") == 0) {
		m.rcm_fault = false;
	} else if (strcmp(argv[1], "dead") == 0) {
		m.rcm_healthy = false; /* reagiert nicht auf den Testimpuls */
	} else if (strcmp(argv[1], "err") == 0 && argc == 3) {
		r = parse_bool(sh, argv[2], &m.rcm_err);
	} else {
		r = -EINVAL;
	}
	irq_unlock(key);
	if (r == -EINVAL) {
		shell_error(sh, "ok|fault|clear|dead|err <0|1> erwartet");
	}
	return r;
}

static int cmd_actuator(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	if (strcmp(argv[1], "ok") == 0 || strcmp(argv[1], "stuck") == 0) {
		m.stuck = argv[1][0] == 's';
		return 0;
	}
	shell_error(sh, "ok|stuck erwartet");
	return -EINVAL;
}

static int cmd_weld(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return parse_bool(sh, argv[1], &m.welded);
}

static int cmd_link(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return parse_bool(sh, argv[1], &m.link);
}

static int cmd_supply(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(argc);
	m.supply_mv = (int32_t)strtol(argv[1], NULL, 0);
	return 0;
}

static int cmd_hang(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	/* Messbloecke anhalten: die Messueberwachung (SOLL-RT-10) muss ausloesen */
	shell_print(sh, "Messbloecke angehalten");
	k_timer_stop(&model_timer);
	return 0;
}

static int cmd_meter(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "energy") == 0) {
		sm.energy_uj = strtoll(argv[2], NULL, 0) * 3600000000LL;
		return 0;
	}
	for (size_t i = 0; i < ARRAY_SIZE(meter_modes); i++) {
		if (argc == 2 && strcmp(argv[1], meter_modes[i]) == 0) {
			sm.mode = (enum meter_mode)i;
			return 0;
		}
	}
	shell_error(sh, "ok|off|frame|flaky|bad oder energy <Wh>");
	return -EINVAL;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sim,
	SHELL_CMD_ARG(status, NULL, "Modell und Ausgaenge anzeigen", cmd_status, 1, 0),
	SHELL_CMD_ARG(car, NULL, "<A..E> Fahrzeugzustand am CP (E = Kurzschluss)", cmd_car, 2, 0),
	SHELL_CMD_ARG(diode, NULL, "<0|1> Fahrzeugdiode vorhanden", cmd_diode, 2, 0),
	SHELL_CMD_ARG(cable, NULL, "<0|13|20|32|63|invalid> Kabel am PP", cmd_cable, 2, 0),
	SHELL_CMD_ARG(din, NULL, "<en|ml|xr|in> <0|1> Digitaleingang", cmd_din, 3, 0),
	SHELL_CMD_ARG(ld, NULL, "<none|permanent|pulse> LD-Rueckmeldung des Aktors", cmd_ld, 2,
		      0),
	SHELL_CMD_ARG(rcm, NULL, "<ok|fault|clear|dead|err 0|1> Fehlerstromsensor", cmd_rcm, 2, 1),
	SHELL_CMD_ARG(actuator, NULL, "<ok|stuck> Verriegelungsaktor", cmd_actuator, 2, 0),
	SHELL_CMD_ARG(weld, NULL, "<0|1> Schuetz verschweisst (Sense bleibt aktiv)", cmd_weld, 2, 0),
	SHELL_CMD_ARG(link, NULL, "<0|1> Netzverbindung (Netzwerk-LED)", cmd_link, 2, 0),
	SHELL_CMD_ARG(supply, NULL, "<mV> Versorgungsmessung", cmd_supply, 2, 0),
	SHELL_CMD_ARG(hang, NULL, "Messbloecke anhalten (Ueberwachung pruefen)", cmd_hang, 1, 0),
	SHELL_CMD_ARG(meter, NULL, "<ok|off|frame|flaky|bad> | energy <Wh>  Energiezaehler",
		      cmd_meter, 2, 1),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(sim, &sub_sim, "Simulierte Hardware (nur native_sim)", cmd_status);
