/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shell-Kommandos (Soll-Spezifikation 12.3). Alle Zugriffe ueber die
 * Parametertabelle mit Grenzpruefung (SOLL-SH-02, SOLL-SH-20).
 */
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <wallfree/api/evse_api.h>
#include <wallfree/api/meter_json.h>
#include <wallfree/evse/param.h>
#include "evse_task.h"
#include "log_ring.h"
#include "meter_task.h"
#include "mqtt_task.h"
#include "netcfg.h"
#include "storage.h"
#include "sysinfo.h"

static const char *const state_names[] = {
	"S0 INIT", "S1 IDLE", "S2 LOCKING", "S3 WAIT", "S4 SELFTEST", "S5 PWM_ON",
	"S6 CHARGING", "S7 STOPPING", "S8 UNLOCKING", "S9 ERROR", "S10 UNAVAILABLE",
};

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct evse *e = evse_acquire();
	int st = e->state;
	char cp = (char)('A' + e->status);
	uint16_t duty = e->out.cp_duty;
	int32_t cur = e->cur.i_ma, allowed = e->cur.allowed_ma;
	bool contactor = e->out.contactor;
	uint32_t err = e->errors, warn = e->warnings;
	int lock = e->lock.state;
	evse_release();

	shell_print(sh, "Zustand  %s", st < (int)ARRAY_SIZE(state_names) ? state_names[st] : "?");
	shell_print(sh, "CP       %c, Tastgrad %u.%u %%", cp, duty / 10, duty % 10);
	shell_print(sh, "Strom    Soll %d mA, Fahrzeug darf %d mA", cur, allowed);
	shell_print(sh, "Schuetz  %s, Verriegelung %d", contactor ? "ein" : "aus", lock);
	shell_print(sh, "Fehler   0x%08x, Warnungen 0x%x", err, warn);
	shell_print(sh, "System   %s, Resetgrund %s, Laufzeit %lld s", sysinfo_version(),
		    wf_reset_name(sysinfo_reset_reason()), (long long)(k_uptime_get() / 1000));
	return 0;
}

static const char *kind_name(uint8_t k)
{
	return k == EVSE_PK_CONFIG ? "cfg" : k == EVSE_PK_RUNTIME ? "rt" : "ro";
}

/* optional nur Namen mit diesem Anfang: die ganze Liste passt nicht in die Konsole */
static int cmd_param_list(const struct shell *sh, size_t argc, char **argv)
{
	const char *pre = argc > 1 ? argv[1] : "";

	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);

		if (strncmp(p->name, pre, strlen(pre)) != 0) {
			continue;
		}
		struct evse *e = evse_acquire();
		int32_t v = evse_param_get(e, p);

		evse_release();
		shell_print(sh, "%-26s %-3s %10d %-8s %s", p->name, kind_name(p->kind), v, p->unit,
			    p->help);
	}
	return 0;
}

static int cmd_param_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	const struct evse_param *p = evse_param_find(argv[1]);

	if (p == NULL) {
		shell_error(sh, "%s", evse_param_strerror(EVSE_PARAM_E_UNKNOWN));
		return -ENOENT;
	}
	struct evse *e = evse_acquire();
	int32_t v = evse_param_get(e, p);

	evse_release();
	shell_print(sh, "%s = %d %s", p->name, v, p->unit);
	if (p->kind != EVSE_PK_STATUS) {
		shell_print(sh, "Bereich %d..%d, Default %d", p->min, p->max, p->def);
	}
	return 0;
}

static int cmd_param_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	char *end;
	long v = strtol(argv[2], &end, 0);

	if (*end != '\0') {
		shell_error(sh, "keine Zahl: %s", argv[2]);
		return -EINVAL;
	}
	const struct evse_param *p = evse_param_find(argv[1]);
	struct evse *e = evse_acquire();
	int err = evse_param_set(e, p, (int32_t)v, k_uptime_get());

	evse_release();
	if (err != EVSE_PARAM_OK) {
		shell_error(sh, "abgelehnt: %s", evse_param_strerror(err));
		return -EINVAL;
	}
	if (!evse_param_persistent(p)) {
		shell_print(sh, "ok (nicht persistent)");
		return 0;
	}
	/* SOLL-PAR-03: wirkt sofort, wird angehaengt */
	int r = storage_save_param(p, (int32_t)v);

	if (r == NVM_OK) {
		shell_print(sh, "ok, gespeichert");
	} else if (r == NVM_PENDING) {
		shell_warn(sh, "ok, Speichern ausstehend (Bereich voll, Verdichten im sicheren Zustand)");
	} else {
		shell_error(sh, "wirksam, aber nicht gespeichert (Fehler %d)", r);
		return -EIO;
	}
	return 0;
}

static int cmd_ack(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct evse *e = evse_acquire();
	bool ok = evse_ack_contactor_fault(e);

	evse_release();
	if (!ok) {
		shell_error(sh, "nicht moeglich (Fehler noch erkannt oder falscher Zustand)");
		return -EBUSY;
	}
	shell_print(sh, "Schuetzfehler quittiert");
	return 0;
}

static const char *const nvm_state_names[] = {"nicht eingehaengt", "gueltig", "leer",
					       "ungueltig"};

static int cmd_nvm(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct storage_status st;

	storage_get_status(&st);
	shell_print(sh, "Bereich   Sektor 7, %s", nvm_state_names[st.state]);
	shell_print(sh, "Belegt    %u B, frei %u B", st.used, st.free);
	shell_print(sh, "Ausstehend %u Werte nur im RAM%s", st.pending,
		    st.pending ? " (Verdichten im sicheren Zustand)" : "");
	shell_print(sh, "Verdichtet %u mal, letzter Fehler %d", st.compactions, st.last_error);
	if (st.flash_sr_boot != 0 || st.flash_sr_early != 0) {
		shell_warn(sh, "FLASH_SR-Fehlerbits beim Start 0x%02x (geloescht), vor den Treibern 0x%02x",
			   st.flash_sr_boot, st.flash_sr_early);
	}
	if (st.damaged) {
		shell_warn(sh, "Rest nach unterbrochenem Schreiben unbrauchbar, wird verdichtet");
	}
	if (st.invalid_at_boot) {
		shell_warn(sh, "Bereich war beim Start ungueltig: Werkseinstellungen (SOLL-NVM-05)");
	}
	return 0;
}

/* SOLL-SH-08 */
static int cmd_factory_reset(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2 || strcmp(argv[1], "confirm") != 0) {
		shell_print(sh, "Werkseinstellungen: 'factory-reset confirm' (danach Neustart)");
		return -EINVAL;
	}
	int r = storage_factory_reset();

	if (r == -EBUSY) {
		shell_error(sh, "nur ohne Fahrzeug in S1 (sicherer Zustand)");
		return r;
	}
	if (r != 0) {
		shell_error(sh, "fehlgeschlagen: %d", r);
		return -EIO;
	}
	shell_print(sh, "Werkseinstellungen geschrieben, Neustart");
	(void)evse_task_reboot_orderly(WF_RST_FACTORY);
	return 0;
}

static const char *const import_names[] = {"uebernommen", "uebernommen (im Original nicht gespeichert = 0)",
					   "abgelehnt", "uebernommen, Speichern fehlgeschlagen"};

static void import_report(void *ctx, uint16_t field, const char *name, int32_t value,
			  enum evse_import_result res, int err)
{
	const struct shell *sh = ctx;

	if (res == EVSE_IMP_REJECTED) {
		shell_warn(sh, "%4u %-22s %6d %s: %s", field, name, value, import_names[res],
			   evse_param_strerror(err));
	} else {
		shell_print(sh, "%4u %-22s %6d %s", field, name, value, import_names[res]);
	}
}

/* SOLL-SH-09 */
static int cmd_import(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	int r = storage_import_orig(import_report, (void *)sh);

	if (r == EVSE_IMP_E_UNSAFE) {
		shell_error(sh, "nur ohne Fahrzeug in S1 (sicherer Zustand)");
		return -EBUSY;
	}
	if (r == EVSE_IMP_E_ORIG) {
		shell_error(sh, "Originalbereich (Sektor 3) ungueltig oder leer");
		return -ENOENT;
	}
	if (r < 0) {
		shell_error(sh, "fehlgeschlagen: %d", r);
		return -EIO;
	}
	shell_print(sh, "%d Werte der Ladelogik uebernommen", r);

	/* Netzwerk: Feld 404 DHCP, 315..326 Adressen (wirkt nach Neustart) */
	const char *err;
	int n = netcfg_import_orig(storage_orig_lookup, NULL, &err);

	if (n < 0) {
		shell_warn(sh, "Netzwerkwerte nicht uebernommen: %s", err);
	} else {
		shell_print(sh, "%d Netzwerkwerte uebernommen (wirken nach Neustart)", n);
	}
	return 0;
}

/* ------------------------------------------------ Laufzeitwerte (SOLL-SH-03..05) */

/* Schreiben ueber die Parametertabelle: gleiche Pruefung wie HTTP (SOLL-SH-20) */
static int set_rt(const struct shell *sh, const char *name, const char *text)
{
	char *end;
	long v = strtol(text, &end, 0);

	if (*end != '\0') {
		shell_error(sh, "keine Zahl: %s", text);
		return -EINVAL;
	}
	int err = evse_param_set_by_name(evse_acquire(), name, (int32_t)v, k_uptime_get());

	evse_release();
	if (err != EVSE_PARAM_OK) {
		shell_error(sh, "%s abgelehnt: %s", name, evse_param_strerror(err));
		return -EINVAL;
	}
	shell_print(sh, "%s = %ld", name, v);
	return 0;
}

static int cmd_current(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "external_current", argv[1]);
}

static int cmd_enable(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "enable_register", argv[1]);
}

static int cmd_avail(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "available_register", argv[1]);
}

static int cmd_pause(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "pause_request", argv[1]);
}

static int cmd_pwm5(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "pwm5_request", argv[1]);
}

static int cmd_lock(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	return set_rt(sh, "lock_request", argv[1]);
}

static int cmd_unlock(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return set_rt(sh, "unlock_request", "1");
}

/* ------------------------------------------------ Fehler (SOLL-SH-06, SOLL-ERR-01/03) */

static void print_mask(const struct shell *sh, const char *prefix, evse_err_t m)
{
	char line[160];
	int n = snprintf(line, sizeof(line), "%s", prefix);

	for (unsigned int i = 0; api_err_name(i) != NULL; i++) {
		if ((m & (1u << i)) && n < (int)sizeof(line)) {
			n += snprintf(line + n, sizeof(line) - n, " %s", api_err_name(i));
		}
	}
	shell_print(sh, "%s", m ? line : prefix);
}

static int cmd_errors(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	static struct evse_err_event hist[EVSE_ERR_HIST];
	struct evse *e = evse_acquire();
	evse_err_t act = e->errors, seen = e->errors_seen;
	size_t n = evse_err_hist_count(e);
	int64_t now = k_uptime_get();

	for (size_t i = 0; i < n; i++) {
		hist[i] = *evse_err_hist_at(e, i);
	}
	evse_release();

	if (act == 0) {
		shell_print(sh, "aktiv: keine");
	}
	for (unsigned int i = 0; api_err_name(i) != NULL; i++) {
		if (act & (1u << i)) {
			shell_print(sh, "aktiv: ERR-%-13s loescht: %s", api_err_name(i), api_err_reset(i));
		}
	}
	print_mask(sh, "seit Start:", seen);
	shell_print(sh, "Historie (juengste zuerst, vor s):");
	for (size_t i = 0; i < n; i++) {
		char pfx[48];

		snprintf(pfx, sizeof(pfx), "  -%6lld s S%-2u +", (long long)((now - hist[i].t) / 1000),
			 hist[i].state);
		if (hist[i].set) {
			print_mask(sh, pfx, hist[i].set);
		}
		if (hist[i].cleared) {
			pfx[strlen(pfx) - 1] = '-';
			print_mask(sh, pfx, hist[i].cleared);
		}
	}
	return 0;
}

static int cmd_errors_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	struct evse *e = evse_acquire();
	bool had = (e->errors & EVSE_ERR_CONTACTOR) != 0;
	bool ok = had && evse_ack_contactor_fault(e);
	evse_err_t left = e->errors;

	evse_release();
	/* SOLL-ERR-03: alle anderen Fehler loeschen sich nach ihrer Regel selbst */
	if (had) {
		shell_print(sh, ok ? "Schuetzfehler quittiert"
				   : "Schuetzfehler nicht quittiert (Ursache besteht oder falscher Zustand)");
	}
	if (left & ~(ok ? EVSE_ERR_CONTACTOR : 0)) {
		print_mask(sh, "weiter aktiv (siehe errors):", left & ~(ok ? EVSE_ERR_CONTACTOR : 0));
	} else {
		shell_print(sh, "keine aktiven Fehler");
	}
	return 0;
}

/* SOLL-SH-07 */
static int cmd_selftest(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	bool ok = evse_selftest_command(evse_acquire(), k_uptime_get());

	evse_release();
	if (!ok) {
		shell_error(sh, "nur in S1 ohne Fahrzeug (CP A) und wenn kein Test laeuft");
		return -EBUSY;
	}
	shell_print(sh, "RCM-Selbsttest gestartet; Ergebnis: param get rcm_selftest "
			"(2 bestanden, 3 fehlgeschlagen)");
	return 0;
}

/* SOLL-SH-11, SOLL-RT-22 */
static int cmd_reboot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (evse_task_reboot_orderly(WF_RST_USER) != 0) {
		shell_print(sh, "Neustart laeuft bereits");
		return 0;
	}
	shell_print(sh, "Neustart: Laden wird geordnet beendet (hoechstens 15 s)");
	return 0;
}

/* SOLL-SH-10, SOLL-BOOT-30..32: nur die Uebergabe; Upload von Hand oder scripts/update.sh */
static int cmd_bootloader(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	char addr[16];
	uint32_t ip, mask;

	netcfg_bootloader_addr(addr, &ip, &mask);
	if (evse_task_update_orderly(ip, mask) != 0) {
		shell_print(sh, "Neustart oder Update laeuft bereits");
		return 0;
	}
	shell_print(sh, "Uebergabe an den Bootloader: Laden endet geordnet (hoechstens 15 s).");
	shell_print(sh, "Dann http://%s/ oeffnen und das Abbild binnen 2 min hochladen", addr);
	shell_print(sh, "(oder scripts/update.sh --upload-only %s datei.bin).", addr);
	return 0;
}

/* Diagnose: Reset-Flags und Register des Bootloaders, wie beim Start vorgefunden */
static int cmd_boot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	const uint32_t *r = sysinfo_boot_regs();

	shell_print(sh, "RCC_CSR  0x%08x (Bit 29 IWDG, 28 Software, 27 POR, 26 Pin)", r[0]);
	shell_print(sh, "BKP10    %u Modus, BKP11 %u Versuche, BKP12 0x%x Ergebnis", r[1], r[2], r[3]);
	shell_print(sh, "BKP13/14 %u.%u.%u.%u / %u.%u.%u.%u, BKP15 %u", r[4] & 0xFF, (r[4] >> 8) & 0xFF,
		    (r[4] >> 16) & 0xFF, r[4] >> 24, r[5] & 0xFF, (r[5] >> 8) & 0xFF,
		    (r[5] >> 16) & 0xFF, r[5] >> 24, r[6]);
	shell_print(sh, "BKP16    %u Starts mit IWDG-Flag (Bootloader, bis Stromausfall)", r[7]);

	uint32_t crc;
	int ok = sysinfo_image(&crc);

	shell_print(sh, "Abbild   %s, CRC32 %08x",
		    ok > 0 ? "gueltig" : ok == 0 ? "UNGUELTIG" : "nicht pruefbar", crc);
	return 0;
}

/* ------------------------------------------------ Zaehler (SOLL-MTR-33) */

static int cmd_meter(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	static const char *const names[METER_Q_COUNT] = {
		"U1 V", "U2 V", "U3 V", "I1 A", "I2 A", "I3 A", "P1 W", "P2 W", "P3 W",
		"P W", "f Hz", "E Bezug kWh", "E Einsp. kWh"};
	static char buf[288];
	struct meter_values mv;
	struct wf_jbuf b;

	if (meter_task_type() == METER_NONE) {
		shell_print(sh, "kein Zaehler konfiguriert (sys set meter_type ...)");
		return 0;
	}
	(void)meter_task_get(&mv);
	shell_print(sh, "Zaehler  %s, Zustand %s, Alter %lld ms", meter_type_name(mv.type),
		    meter_status_name(mv.status), (long long)mv.age_ms);
	for (int q = 0; q < METER_Q_COUNT; q++) {
		if (!meter_has(mv.type, (enum meter_q)q)) {
			continue;
		}
		wf_jb_init(&b, buf, sizeof(buf));
		if (mv.valid[q]) {
			wf_jb_milli(&b, mv.v[q]);
		} else {
			wf_jb_raw(&b, "ungueltig");
		}
		shell_print(sh, "  %-13s %s", names[q], buf);
	}
	wf_jb_init(&b, buf, sizeof(buf));
	wf_meter_state_json(&mv, &b);
	shell_print(sh, "Zaehler  %s", buf);
	return 0;
}

/* ------------------------------------------------ MQTT (SOLL-MQTT-32) */

static int cmd_mqtt(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	static const char *const conn[] = {"aus", "nicht verbunden", "verbunden", "abgelehnt"};
	static char buf[320];
	struct wf_mq_status s;
	struct wf_jbuf b;

	mqtt_task_status(&s);
	shell_print(sh, "MQTT     %s, Praefix %s", s.conn < ARRAY_SIZE(conn) ? conn[s.conn] : "?",
		    s.prefix);
	wf_jb_init(&b, buf, sizeof(buf));
	wf_mq_status_json(&s, &b);
	shell_print(sh, "MQTT     %s", buf);
	return 0;
}

/* ------------------------------------------------ Systemparameter */

static int cmd_sys_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	static char buf[768];
	struct wf_sysconf a, sv;
	struct wf_jbuf b;

	netcfg_get(&a, &sv);
	wf_jb_init(&b, buf, sizeof(buf));
	wf_sys_json(&a, &b);
	shell_print(sh, "aktiv:       %s", buf);
	wf_jb_init(&b, buf, sizeof(buf));
	wf_sys_json(&sv, &b);
	shell_print(sh, "gespeichert: %s", buf);
	if (netcfg_restart_required()) {
		shell_warn(sh, "Neustart noetig, damit die gespeicherten Werte wirken");
	}
	return 0;
}

static int cmd_sys_set(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	const char *err = netcfg_set_text(argv[1], argv[2]);

	if (err != NULL) {
		shell_error(sh, "abgelehnt: %s", err);
		return -EINVAL;
	}
	shell_print(sh, strncmp(argv[1], "mqtt_", 5) == 0 ? "gespeichert, wirkt sofort"
							  : "gespeichert, wirkt nach Neustart");
	return 0;
}

/*
 * SOLL-SH-21: Ersatz fuer Zephyrs kernel thread stacks, nur lesend (kernel enthaelt auch
 * suspend/kill). Ohne Sperre durchlaufen wie dort: die Ausgabe darf nicht unter
 * gesperrten Interrupts laufen.
 */
static void thread_line(const struct k_thread *t, void *user_data)
{
	const struct shell *sh = user_data;
	size_t size = t->stack_info.size, unused;
	const char *name = k_thread_name_get((struct k_thread *)t);

	if (k_thread_stack_space_get(t, &unused) != 0) {
		unused = size;
	}
	shell_print(sh, "%-20s %4d %5u/%-5u %3u %%", name != NULL && name[0] ? name : "?",
		    k_thread_priority_get((struct k_thread *)t), (unsigned int)(size - unused),
		    (unsigned int)size, size ? (unsigned int)((size - unused) * 100 / size) : 0);
}

K_KERNEL_STACK_ARRAY_DECLARE(z_interrupt_stacks, CONFIG_MP_MAX_NUM_CPUS, CONFIG_ISR_STACK_SIZE);

static int cmd_threads(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "%-20s %4s %11s", "Thread", "Prio", "Stack");
	k_thread_foreach_unlocked(thread_line, (void *)sh);
#ifndef CONFIG_ARCH_POSIX
	/* Interrupt-Stack: ungenutzte Bytes ab dem unteren Ende (CONFIG_INIT_STACKS) */
	const uint8_t *buf = K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]);
	size_t size = K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]), unused = 0;

	while (unused < size && buf[unused] == 0xaa) {
		unused++;
	}
	shell_print(sh, "%-20s %4s %5u/%-5u %3u %%", "IRQ", "", (unsigned int)(size - unused),
		    (unsigned int)size, (unsigned int)((size - unused) * 100 / size));
#endif
	return 0;
}

/* SOLL-SH-22: juengste Logmeldungen, "clear" leert den Ring */
static int cmd_log(const struct shell *sh, size_t argc, char **argv)
{
	if (argc > 1) {
		if (strcmp(argv[1], "clear") != 0) {
			shell_error(sh, "nur: log [clear]");
			return -EINVAL;
		}
		log_ring_clear();
		return 0;
	}
	log_ring_print(sh);
	return 0;
}

static int cmd_uptime(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "Laufzeit %lld ms", (long long)k_uptime_get());
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_errors,
	SHELL_CMD_ARG(clear, NULL, "quittieren (nur Schuetzfehler; andere loeschen sich selbst)",
		      cmd_errors_clear, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_sys,
	SHELL_CMD_ARG(list, NULL, "Netz- und Systemparameter", cmd_sys_list, 1, 0),
	SHELL_CMD_ARG(set, NULL, "<name> <wert> (mqtt_* sofort, sonst nach Neustart)", cmd_sys_set,
		      3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_param,
	SHELL_CMD_ARG(list, NULL, "[Anfang] Parameter, optional nur Namen mit diesem Anfang",
		      cmd_param_list, 1, 1),
	SHELL_CMD_ARG(get, NULL, "<name>", cmd_param_get, 2, 0),
	SHELL_CMD_ARG(set, NULL, "<name> <wert>", cmd_param_set, 3, 0),
	SHELL_SUBCMD_SET_END);

/* Kommandos direkt an der Wurzel, ohne gemeinsamen Vorsatz (SOLL-SH-19); "help" (Zephyr)
 * listet sie. Namen mit Bindestrich brauchen einen eigenen Bezeichner, sonst wie
 * SHELL_CMD_ARG_REGISTER (Zephyr 4.4.2, bei Updates pruefen).
 */
#define ROOT_CMD_ARG(id, syntax, subcmd, help, handler, mand, opt)                          \
	static const struct shell_static_entry UTIL_CAT(_shell_, id) =                      \
		SHELL_CMD_ARG(syntax, subcmd, help, handler, mand, opt);                    \
	static const TYPE_SECTION_ITERABLE(union shell_cmd_entry, UTIL_CAT(shell_cmd_, id), \
					   shell_root_cmds, UTIL_CAT(shell_cmd_, id)) = {   \
		.entry = &UTIL_CAT(_shell_, id)}

SHELL_CMD_ARG_REGISTER(status, NULL, "Zustand der Ladelogik", cmd_status, 1, 0);
SHELL_CMD_REGISTER(param, &sub_param, "Parameter", NULL);
SHELL_CMD_ARG_REGISTER(ack, NULL, "Schuetzfehler quittieren", cmd_ack, 1, 0);
SHELL_CMD_REGISTER(sys, &sub_sys, "Netz- und Systemparameter", NULL);
SHELL_CMD_ARG_REGISTER(current, NULL, "<mA> Sollstrom Slot external (0, 6000..80000)",
		       cmd_current, 2, 0);
SHELL_CMD_ARG_REGISTER(enable, NULL, "0|1 Freigabe-Register (mit D10)", cmd_enable, 2, 0);
SHELL_CMD_ARG_REGISTER(avail, NULL, "0|1 Verfuegbarkeit (mit D10)", cmd_avail, 2, 0);
SHELL_CMD_ARG_REGISTER(pause, NULL, "0|1 Laden unterbrechen", cmd_pause, 2, 0);
SHELL_CMD_ARG_REGISTER(pwm5, NULL, "0|1 5-%-Modus", cmd_pwm5, 2, 0);
SHELL_CMD_ARG_REGISTER(lock, NULL, "0|1 Verriegelungswunsch (mit D9)", cmd_lock, 2, 0);
SHELL_CMD_ARG_REGISTER(unlock, NULL, "Entriegeln anfordern", cmd_unlock, 1, 0);
SHELL_CMD_ARG_REGISTER(errors, &sub_errors, "aktive Fehler, Historie", cmd_errors, 1, 0);
SHELL_CMD_ARG_REGISTER(selftest, NULL, "RCM-Selbsttest (nur S1, CP A)", cmd_selftest, 1, 0);
SHELL_CMD_ARG_REGISTER(reboot, NULL, "geordneter Neustart (SOLL-RT-22)", cmd_reboot, 1, 0);
SHELL_CMD_ARG_REGISTER(bootloader, NULL, "Update: Uebergabe an den Bootloader (SOLL-SH-10)",
		       cmd_bootloader, 1, 0);
SHELL_CMD_ARG_REGISTER(boot, NULL, "Diagnose: Reset-Flags und Bootloader-Register beim Start",
		       cmd_boot, 1, 0);
SHELL_CMD_ARG_REGISTER(nvm, NULL, "Zustand des Parameterspeichers", cmd_nvm, 1, 0);
SHELL_CMD_ARG_REGISTER(meter, NULL, "Energiezaehler: Messwerte und Fehlerzaehler", cmd_meter,
		       1, 0);
SHELL_CMD_ARG_REGISTER(mqtt, NULL, "MQTT: Verbindung und Zaehler", cmd_mqtt, 1, 0);
SHELL_CMD_ARG_REGISTER(threads, NULL, "Threads mit Prioritaet und Stack-Auslastung",
		       cmd_threads, 1, 0);
SHELL_CMD_ARG_REGISTER(uptime, NULL, "Laufzeit seit dem Start", cmd_uptime, 1, 0);
SHELL_CMD_ARG_REGISTER(log, NULL, "[clear] juengste Logmeldungen (seit dem Start, etwa 1,9 KB)",
		       cmd_log, 1, 1);
ROOT_CMD_ARG(factory_reset, factory-reset, NULL, "confirm: Werkseinstellungen und Neustart",
	     cmd_factory_reset, 1, 1);
ROOT_CMD_ARG(import_original, import-original, NULL,
	     "Einstellungen aus der Originalfirmware uebernehmen", cmd_import, 1, 0);
