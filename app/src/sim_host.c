/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-Seite der Simulation (native_sim, im Runner mit der Host-C-Bibliothek
 * gebaut): Neustart des Prozesses per execv als Ersatz fuer den Systemreset.
 * Vorher werden alle Dateideskriptoren ausser 0..2 geschlossen, damit der
 * HTTP- und der Telnet-Port im neuen Prozess wieder frei sind.
 *
 * stdout ist zeilengepuffert (Log in Datei sofort sichtbar).
 *
 * Zusaetzlich setzt bind() hier SO_REUSEADDR: Zephyrs Telnet-Backend tut das
 * nicht, und nach einem Neustart mit offener Telnet-Verbindung blockiert
 * sonst TIME_WAIT den Port (auf der Steuerung loescht der Reset den
 * TCP-Zustand, dort tritt das nicht auf).
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <nsi_cmdline.h>
#include <nsi_tasks.h>
#include <nsi_tracing.h>
#include "sim_host.h"

/* Log zeilenweise auch in Dateien/Pipes (sonst Blockpuffer: Meldungen
 * erschienen erst Kilobytes spaeter, was wie ein Log-Stillstand aussah) */
static void line_buffered_stdout(void)
{
	setvbuf(stdout, NULL, _IOLBF, 0);
}
NSI_TASK(line_buffered_stdout, PRE_BOOT_1, 0);

/* Deklaration wie glibc mit _GNU_SOURCE (transparente Union) */
int bind(int fd, __CONST_SOCKADDR_ARG addr, socklen_t len)
{
	static int (*real_bind)(int, __CONST_SOCKADDR_ARG, socklen_t);
	int one = 1;

	if (real_bind == NULL) {
		*(void **)&real_bind = dlsym(RTLD_NEXT, "bind"); /* Form nach POSIX dlsym */
	}
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	return real_bind(fd, addr, len);
}

/* sofortiger Neustart wie ein Hardware-Reset (auch aus einer ISR): ohne die
 * Exit-Ablaeufe von Zephyr; die Flash-Datei ist per MAP_SHARED eingeblendet
 * und damit bereits geschrieben */
static void close_all(void)
{
	fflush(stdout);
	fflush(stderr);
	/* syscall statt close(): close loest im gemeinsamen Programm sonst
	 * Zephyrs close() auf (zvfs) */
	for (int fd = 3; fd < 1024; fd++) {
		(void)syscall(SYS_close, fd);
	}
}

void wf_sim_host_reset_now(void)
{
	char **argv;
	int argc;

	nsi_get_cmd_line_args(&argc, &argv);
	close_all();
	(void)execv("/proc/self/exe", argv);
	nsi_print_error_and_exit("wallfree-sim: Neustart fehlgeschlagen (%s)\n", strerror(errno));
}

void wf_sim_host_exec_bootloader(const char *script, unsigned int port)
{
	char **argv;
	int argc;
	char exe[4096];
	char portstr[16];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);

	if (n <= 0) {
		nsi_print_error_and_exit("wallfree-sim: Programmpfad unbekannt\n");
	}
	exe[n] = '\0';
	snprintf(portstr, sizeof(portstr), "%u", port);
	nsi_get_cmd_line_args(&argc, &argv);
	/* python3 -I skript --port P -- programm argumente... */
	char *args[argc + 8];
	int k = 0;

	args[k++] = "python3";
	args[k++] = "-I";
	args[k++] = (char *)script;
	args[k++] = "--port";
	args[k++] = portstr;
	args[k++] = "--";
	args[k++] = exe;
	for (int i = 1; i < argc; i++) {
		args[k++] = argv[i];
	}
	args[k] = NULL;
	close_all();
	(void)execvp("python3", args);
	nsi_print_error_and_exit("wallfree-sim: Bootloader-Ersatz nicht startbar (%s)\n",
				 strerror(errno));
}

