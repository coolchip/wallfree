/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shell-Kommandos (Soll-Spezifikation 12.3). Alle Zugriffe ueber die
 * Parametertabelle mit Grenzpruefung (SOLL-SH-02, SOLL-SH-20).
 */
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <wallfree/evse/param.h>
#include "evse_task.h"

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
	struct evse *e = evse_acquire();
	int err = evse_param_set_by_name(e, argv[1], (int32_t)v, k_uptime_get());

	evse_release();
	if (err != EVSE_PARAM_OK) {
		shell_error(sh, "abgelehnt: %s", evse_param_strerror(err));
		return -EINVAL;
	}
	/* TODO SOLL-NVM: persistente Parameter speichern */
	shell_print(sh, "ok");
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

SHELL_STATIC_SUBCMD_SET_CREATE(sub_param,
	SHELL_CMD_ARG(list, NULL, "alle Parameter", cmd_param_list, 1, 0),
	SHELL_CMD_ARG(get, NULL, "<name>", cmd_param_get, 2, 0),
	SHELL_CMD_ARG(set, NULL, "<name> <wert>", cmd_param_set, 3, 0),
	SHELL_SUBCMD_SET_END);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_evse,
	SHELL_CMD_ARG(status, NULL, "Zustand der Ladelogik", cmd_status, 1, 0),
	SHELL_CMD(param, &sub_param, "Parameter", NULL),
	SHELL_CMD_ARG(ack, NULL, "Schuetzfehler quittieren", cmd_ack, 1, 0),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(evse, &sub_evse, "Ladelogik", NULL);
