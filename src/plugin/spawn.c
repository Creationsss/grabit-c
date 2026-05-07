// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/spawn.h"

#include "util.h"

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int plugin_run_in(const char *cwd, char *const argv[]) {
	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		if (cwd && chdir(cwd) != 0) _exit(127);
		execvp(argv[0], argv);
		_exit(127);
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 0 : -1;
}

int plugin_read_first_line(const char *path, char *out, size_t cap) {
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	if (!fgets(out, (int)cap, f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	char *nl = strchr(out, '\n');
	if (nl) *nl = '\0';
	return 0;
}
