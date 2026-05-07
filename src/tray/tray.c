// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/tray.h"

#include "log.h"
#include "notify/notify.h"
#include "tray/sni.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

struct tray_state {
	pid_t pid;
};

static volatile sig_atomic_t g_tray_stop = 0;

static void tray_signal(int sig) {
	(void)sig;
	g_tray_stop = 1;
}

static void install_signals(void) {
	grabit_install_signal_handler(SIGTERM, tray_signal);
	grabit_install_signal_handler(SIGINT, tray_signal);
	grabit_install_signal_handler(SIGHUP, tray_signal);
	grabit_ignore_signal(SIGPIPE);
}

struct tray_state *tray_start(void) {
	struct tray_state *t = calloc(1, sizeof *t);
	if (!t) return NULL;

	pid_t pid = fork();
	if (pid < 0) {
		log_warn("tray: fork failed: %s", strerror(errno));
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "tray unavailable; see terminal for details",
			.force = true,
		});
		free(t);
		return NULL;
	}
	if (pid == 0) {
		setpgid(0, 0);
		prctl(PR_SET_PDEATHSIG, SIGTERM);
		if (getppid() == 1) _exit(0);
		install_signals();
		sni_run(&g_tray_stop);
		_exit(0);
	}
	(void)setpgid(pid, pid);
	t->pid = pid;
	return t;
}

void tray_stop(struct tray_state *t) {
	if (!t) return;
	if (t->pid > 0) {
		kill(t->pid, SIGTERM);
		int status = 0;
		while (waitpid(t->pid, &status, 0) < 0 && errno == EINTR) {
			continue;
		}
	}
	free(t);
}
