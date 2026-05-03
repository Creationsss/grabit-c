// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "edit/edit.h"

#include "config.h"
#include "log.h"
#include "notify/notify.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static const char *basename_of(const char *p) {
	const char *slash = strrchr(p, '/');
	return slash ? slash + 1 : p;
}

static int run_satty(const char *bin, const char *path) {
	char tmp[256];
	snprintf(tmp, sizeof tmp, "/tmp/grabit_satty_%d_%lld.png",
			 (int)getpid(), (long long)time(NULL));

	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		char *argv[] = {(char *)bin,
						(char *)"-f", (char *)path,
						(char *)"--output-filename", tmp,
						(char *)"--early-exit",
						(char *)"--copy-command", (char *)"none",
						NULL};
		execvp(bin, argv);
		_exit(127);
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		unlink(tmp);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		log_error("edit: rename(%s -> %s): %s", tmp, path, strerror(errno));
		unlink(tmp);
		return -1;
	}
	return 0;
}

static int run_simple(const char *bin, const char *path) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		char *argv[] = {(char *)bin, (char *)path, NULL};
		execvp(bin, argv);
		_exit(127);
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
	return 0;
}

static int dispatch(const char *bin, const char *path) {
	if (strcmp(basename_of(bin), "satty") == 0) return run_satty(bin, path);
	return run_simple(bin, path);
}

static void notify_setup_needed(const char *body) {
	notify_send(&(struct notify_opts){
		.summary = "grabit: setup needed",
		.body = body,
		.force = true,
	});
}

static void notify_edit_failed(const char *bin) {
	char body[160];
	snprintf(body, sizeof body, "%s exited unsuccessfully; continuing with unedited file",
			 basename_of(bin));
	notify_send(&(struct notify_opts){
		.summary = "grabit: edit failed",
		.body = body,
		.force = true,
	});
}

int grabit_edit_file(struct config *cfg, const char *path) {
	const char *editor = config_get(cfg, "editor");
	if (editor && editor[0]) {
		if (!grabit_in_path(editor)) {
			log_error("edit: configured editor `%s` not found in $PATH", editor);
			log_error("  unset with: grabit unset editor");
			notify_setup_needed("editor not found; see terminal for details");
			return -1;
		}
		int rc = dispatch(editor, path);
		if (rc != 0) notify_edit_failed(editor);
		return rc;
	}
	static const char *const CANDIDATES[] = {"satty", "swappy", "gimp", "krita", "kolourpaint", NULL};
	for (size_t i = 0; CANDIDATES[i]; i++) {
		if (grabit_in_path(CANDIDATES[i])) {
			int rc = dispatch(CANDIDATES[i], path);
			if (rc != 0) notify_edit_failed(CANDIDATES[i]);
			return rc;
		}
	}
	log_error("edit: no editor found in $PATH (tried: satty, swappy, gimp, krita, kolourpaint)");
	log_error("  install one or set: grabit set editor <bin>");
	notify_setup_needed("no editor installed; see terminal for details");
	return -1;
}
