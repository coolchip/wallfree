/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Host-Seite der Simulation (native_sim, im Runner mit der Host-C-Bibliothek
 * gebaut): Neustart des Prozesses per execv als Ersatz fuer den Systemreset.
 * Vorher werden alle Dateideskriptoren ausser 0..2 geschlossen, damit der
 * HTTP- und der Telnet-Port im neuen Prozess wieder frei sind.
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
#include <nsi_tracing.h>
#include "sim_host.h"

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
	static int (*real_bind)(int, const struct sockaddr *, socklen_t);
	int one = 1;

	if (real_bind == NULL) {
		real_bind = (int (*)(int, const struct sockaddr *, socklen_t))dlsym(RTLD_NEXT,
										     "bind");
	}
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	return real_bind(fd, addr, len);
}

/* sofortiger Neustart wie ein Hardware-Reset (auch aus einer ISR): ohne die
 * Exit-Ablaeufe von Zephyr; die Flash-Datei ist per MAP_SHARED eingeblendet
 * und damit bereits geschrieben */
void wf_sim_host_reset_now(void)
{
	char **argv;
	int argc;

	fflush(stdout);
	fflush(stderr);
	nsi_get_cmd_line_args(&argc, &argv);
	/* syscall statt close(): close loest im gemeinsamen Programm sonst
	 * Zephyrs close() auf (zvfs) */
	for (int fd = 3; fd < 1024; fd++) {
		(void)syscall(SYS_close, fd);
	}
	(void)execv("/proc/self/exe", argv);
	nsi_print_error_and_exit("wallfree-sim: Neustart fehlgeschlagen (%s)\n", strerror(errno));
}

