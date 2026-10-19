/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shell-Kommandos (Soll-Spezifikation 12.3). Alle Zugriffe ueber die
 * Parametertabelle mit Grenzpruefung (SOLL-SH-02, SOLL-SH-20).
 */
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <string.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/reboot.h>
#include <wallfree/evse/param.h>
#include "evse_task.h"
#include "storage.h"

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
	return 0;
}

static const char *kind_name(uint8_t k)
{
	return k == EVSE_PK_CONFIG ? "cfg" : k == EVSE_PK_RUNTIME ? "rt" : "ro";
}

static int cmd_param_list(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	for (size_t i = 0; i < evse_param_count(); i++) {
		const struct evse_param *p = evse_param_at(i);
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
		shell_print(sh, "Werkseinstellungen: 'evse factory-reset confirm' (danach Neustart)");
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
	k_msleep(100);
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

static const char *const import_names[] = {"uebernommen", "nicht gespeichert im Original",
					   "abgelehnt", "noch nicht unterstuetzt",
					   "uebernommen, Speichern fehlgeschlagen"};

static void import_report(void *ctx, uint16_t field, const char *name, int32_t value,
			  enum evse_import_result res, int err)
{
	const struct shell *sh = ctx;

	if (res == EVSE_IMP_MISSING) {
		shell_print(sh, "%4u %-22s -", field, name);
	} else if (res == EVSE_IMP_REJECTED) {
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
	shell_print(sh, "%d Werte uebernommen", r);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_param,
	SHELL_CMD_ARG(list, NULL, "alle Parameter", cmd_param_list, 1, 0),
	SHELL_CMD_ARG(get, NULL, "<name>", cmd_param_get, 2, 0),
	SHELL_CMD_ARG(set, NULL, "<name> <wert>", cmd_param_set, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_evse,
	SHELL_CMD_ARG(status, NULL, "Zustand der Ladelogik", cmd_status, 1, 0),
	SHELL_CMD(param, &sub_param, "Parameter", NULL),
	SHELL_CMD_ARG(ack, NULL, "Schuetzfehler quittieren", cmd_ack, 1, 0),
	SHELL_CMD_ARG(nvm, NULL, "Zustand des Parameterspeichers", cmd_nvm, 1, 0),
	SHELL_CMD_ARG(factory-reset, NULL, "confirm: Werkseinstellungen und Neustart",
		      cmd_factory_reset, 1, 1),
	SHELL_CMD_ARG(import-original, NULL, "Einstellungen aus der Originalfirmware uebernehmen",
		      cmd_import, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(evse, &sub_evse, "Ladelogik", NULL);
