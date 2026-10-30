/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Shell fuer die Konsole der Weboberflaeche (SOLL-SH-19): eigenes Backend ohne Ein- und
 * Ausgabekanal. shell_web_exec() fuehrt eine Kommandozeile im aufrufenden Thread aus (dem
 * HTTP-Server) und schreibt die Ausgabe in den Puffer des Aufrufers, sodass kein eigener
 * Ausgabepuffer im RAM liegt. Zephyrs Dummy-Backend tut dasselbe mit festem Puffer.
 *
 * Die Shell legt trotzdem einen Thread an; er wartet nur (keine Eingabe, kein Log-Backend).
 * Schreibt er (Prompt beim Start), wird das verworfen.
 */
#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <wallfree/api/json.h>
#include "shell_web.h"

/* Zephyr-intern (subsys/shell/shell_utils.h, Zephyr 4.4.2): n-tes Unterkommando von parent
 * (NULL = Wurzel), loest dynamische und Sektionslisten auf; bei Zephyr-Updates pruefen */
const struct shell_static_entry *z_shell_cmd_get(const struct shell_static_entry *parent,
						 size_t idx, struct shell_static_entry *dloc);

static struct {
	k_tid_t tid;   /* Thread, dessen Ausgabe gerade gesammelt wird */
	char *buf;
	size_t size;
	size_t len;
	bool cut;
} cap;

static int web_init(const struct shell_transport *transport, const void *config,
		    shell_transport_handler_t evt_handler, void *context)
{
	ARG_UNUSED(transport);
	ARG_UNUSED(config);
	ARG_UNUSED(evt_handler);
	ARG_UNUSED(context);
	return 0;
}

static int web_uninit(const struct shell_transport *transport)
{
	ARG_UNUSED(transport);
	return 0;
}

static int web_enable(const struct shell_transport *transport, bool blocking)
{
	ARG_UNUSED(transport);
	ARG_UNUSED(blocking);
	return 0;
}

static int web_write(const struct shell_transport *transport, const void *data, size_t length,
		     size_t *cnt)
{
	ARG_UNUSED(transport);
	*cnt = length; /* immer alles abnehmen, sonst wartet die Shell auf TXDONE */
	if (cap.buf == NULL || k_current_get() != cap.tid) {
		return 0;
	}
	size_t n = MIN(length, cap.size - 1 - cap.len);

	memcpy(cap.buf + cap.len, data, n);
	cap.len += n;
	cap.cut = cap.cut || n < length;
	return 0;
}

static int web_read(const struct shell_transport *transport, void *data, size_t length,
		    size_t *cnt)
{
	ARG_UNUSED(transport);
	ARG_UNUSED(data);
	ARG_UNUSED(length);
	*cnt = 0;
	return 0;
}

static const struct shell_transport_api web_api = {
	.init = web_init,
	.uninit = web_uninit,
	.enable = web_enable,
	.write = web_write,
	.read = web_read,
};

static struct shell_transport web_transport = {.api = &web_api, .ctx = NULL};

SHELL_DEFINE(shell_web, "", &web_transport, 0, 0, SHELL_FLAG_CRLF_DEFAULT);

int shell_web_exec(const char *line, char *buf, size_t size, bool *cut)
{
	if (size == 0) {
		return -EINVAL;
	}
	cap.tid = k_current_get();
	cap.size = size;
	cap.len = 0;
	cap.cut = false;
	cap.buf = buf;
	int err = shell_execute_cmd(&shell_web, line);

	cap.buf = NULL;
	buf[cap.len] = '\0';
	*cut = cap.cut;
	if (err == -ENOMEM || (err == -ENOEXEC && cap.len == 0)) {
		/* zu lang bzw. Shell belegt: ohne Ausgabe */
		return err;
	}
	return (int)cap.len;
}

/* ------------------------------------------------ Kommandobaum (SOLL-SH-23) */

#define PATH_MAX_LEN 64
#define DEPTH_MAX    4

struct walk {
	size_t cursor; /* erster auszugebender Eintrag */
	size_t i;      /* laufende Nummer */
	struct wf_jbuf *b;
	bool full;
	char path[PATH_MAX_LEN];
};

static void walk(struct walk *w, const struct shell_static_entry *parent, size_t plen, int depth)
{
	struct shell_static_entry dloc;
	const struct shell_static_entry *e;

	for (size_t k = 0; !w->full && (e = z_shell_cmd_get(parent, k, &dloc)) != NULL; k++) {
		size_t n = strlen(e->syntax);

		if (plen + 1 + n >= sizeof(w->path)) {
			continue;
		}
		size_t len = plen;

		if (plen > 0) {
			w->path[len++] = ' ';
		}
		memcpy(w->path + len, e->syntax, n + 1);
		len += n;
		if (w->i >= w->cursor) {
			size_t mark = w->b->len;
			const char *help = e->help && !shell_help_is_structured(e->help) ? e->help : "";

			wf_jb_raw(w->b, w->i == 0 ? "[" : ",[");
			wf_jb_str(w->b, w->path);
			wf_jb_raw(w->b, ",");
			wf_jb_str(w->b, help);
			wf_jb_raw(w->b, "]");
			if (w->b->overflow) {
				w->b->len = mark;
				w->b->p[mark] = '\0';
				w->b->overflow = false;
				w->full = true;
				return;
			}
		}
		w->i++;
		/* dloc wird beim Abstieg ueberschrieben: Unterbaum nur bei statischen Eintraegen */
		if (e != &dloc && depth + 1 < DEPTH_MAX) {
			walk(w, e, len, depth + 1);
		}
	}
}

size_t shell_web_commands_json(size_t cursor, struct wf_jbuf *b)
{
	struct walk w = {.cursor = cursor, .b = b};

	if (cursor == 0) {
		wf_jb_raw(b, "[");
	}
	walk(&w, NULL, 0, 0);
	if (w.full) {
		return w.i;
	}
	size_t mark = b->len;

	wf_jb_raw(b, "]");
	if (b->overflow) {
		b->len = mark;
		b->p[mark] = '\0';
		b->overflow = false;
		return w.i; /* nur noch die Klammer */
	}
	return SHELL_WEB_DONE;
}

static int shell_web_init(void)
{
	/* ohne Echo, Farben und VT100-Steuerfolgen: reiner Text fuer den Browser */
	static const struct shell_backend_config_flags flags = {
		.insert_mode = 0, .echo = 0, .obscure = 0, .mode_delete = 1,
		.use_colors = 0, .use_vt100 = 0,
	};

	return shell_init(&shell_web, NULL, flags, false, 0);
}

SYS_INIT(shell_web_init, APPLICATION, 0);
