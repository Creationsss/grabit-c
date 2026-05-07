/* SPDX-License-Identifier: AGPL-3.0-or-later
 * Copyright (C) 2026 creations
 *
 * grabit-plugin.h - drop-in header for grabit plugins.
 *
 * Plugins are standalone binaries dispatched by grabit when invoked as
 *   grabit <name> [args...]
 *
 * grabit sets these env vars before exec:
 *   GRABIT_BIN          absolute path to the running grabit binary
 *   GRABIT_PLUGIN_NAME  the plugin's name (matches grabit-<name> on disk)
 *   GRABIT_PLUGIN_DIR   the plugin's install dir (manifest + bundled data)
 *   GRABIT_CACHE_DIR    a per-plugin cache dir (created if missing)
 *
 * Single-header, no link-time dep. Vendor a copy in your plugin repo.
 */

#ifndef GRABIT_PLUGIN_H
#define GRABIT_PLUGIN_H

#if !defined(_XOPEN_SOURCE) && !defined(_POSIX_C_SOURCE) && !defined(_GNU_SOURCE)
#define _XOPEN_SOURCE 700
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static inline const char *grabit_plugin_bin(void) {
	const char *v = getenv("GRABIT_BIN");
	if (v && v[0]) return v;
	static char self[1024];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n <= 0) return "grabit";
	self[n] = '\0';
	char *slash = strrchr(self, '/');
	if (!slash) return "grabit";
	static char sibling[1100];
	snprintf(sibling, sizeof sibling, "%.*s/grabit", (int)(slash - self), self);
	return access(sibling, X_OK) == 0 ? sibling : "grabit";
}

static inline const char *grabit_plugin_dir(void) {
	const char *v = getenv("GRABIT_PLUGIN_DIR");
	return (v && v[0]) ? v : ".";
}

static inline const char *grabit_plugin_cache_dir(void) {
	const char *v = getenv("GRABIT_CACHE_DIR");
	return (v && v[0]) ? v : "/tmp";
}

static inline const char *grabit_plugin_name(void) {
	const char *v = getenv("GRABIT_PLUGIN_NAME");
	return (v && v[0]) ? v : "plugin";
}

/* Pin a file via grabit. Replaces the current process on success. */
static inline int grabit_plugin_pin(const char *file) {
	const char *bin = grabit_plugin_bin();
	char *const argv[] = {(char *)bin, "--pin", "-f", (char *)file, NULL};
	execvp(bin, argv);
	return -1;
}

/* Capture a screenshot via `grabit -o` and return the saved path.
 * Caller free()s the returned string. NULL on failure. */
static inline char *grabit_plugin_capture(void) {
	int pipefd[2];
	if (pipe(pipefd) != 0) return NULL;
	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return NULL;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		const char *bin = grabit_plugin_bin();
		char *const argv[] = {(char *)bin, "-o", NULL};
		execvp(bin, argv);
		_exit(127);
	}
	close(pipefd[1]);
	char buf[4096];
	size_t off = 0;
	ssize_t r;
	while (off < sizeof buf - 1 &&
		   (r = read(pipefd[0], buf + off, sizeof buf - 1 - off)) > 0) {
		off += (size_t)r;
	}
	close(pipefd[0]);
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) break;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) return NULL;
	buf[off] = '\0';
	while (off > 0 && (buf[off - 1] == '\n' || buf[off - 1] == '\r' ||
					   buf[off - 1] == ' ')) {
		buf[--off] = '\0';
	}
	if (off == 0) return NULL;
	char *out = (char *)malloc(off + 1);
	if (!out) return NULL;
	memcpy(out, buf, off + 1);
	return out;
}

#endif
