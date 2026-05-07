// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/dispatch.h"

#include "log.h"
#include "paths.h"
#include "plugin/plugin.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

void plugin_dispatch_set_env(const char *name) {
	char self[1024];
	ssize_t sn = readlink("/proc/self/exe", self, sizeof self - 1);
	if (sn > 0) {
		self[sn] = '\0';
		setenv("GRABIT_BIN", self, 1);
	}
	setenv("GRABIT_PLUGIN_NAME", name, 1);

	char *plugin_dir = NULL;
	if (grabit_xasprintf(&plugin_dir, "%s/%s", plugin_dir_path(), name) == 0) {
		setenv("GRABIT_PLUGIN_DIR", plugin_dir, 1);
		free(plugin_dir);
	}

	const char *cache_home = getenv("XDG_CACHE_HOME");
	const char *home = getenv("HOME");
	char *cache = NULL;
	if (cache_home && cache_home[0] == '/') {
		grabit_xasprintf(&cache, "%s/grabit/plugins/%s", cache_home, name);
	} else if (home) {
		grabit_xasprintf(&cache, "%s/.cache/grabit/plugins/%s", home, name);
	}
	if (cache) {
		paths_mkdir_p(cache);
		setenv("GRABIT_CACHE_DIR", cache, 1);
		free(cache);
	}
}

static const char *last_line(struct grabit_buf *b) {
	if (!b->data) return "";
	while (b->len > 0 && (b->data[b->len - 1] == '\n' ||
						  b->data[b->len - 1] == '\r' ||
						  b->data[b->len - 1] == ' ')) {
		b->data[--b->len] = '\0';
	}
	char *nl = strrchr(b->data, '\n');
	return nl ? nl + 1 : b->data;
}

int plugin_dispatch_pin(const char *name, int argc, char **argv) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return 1;
	}
	char path[1024];
	if (plugin_resolve(name, path, sizeof path) != 0) {
		log_error("plugin: %s not installed", name);
		return 1;
	}
	plugin_maybe_auto_update(name);
	plugin_dispatch_set_env(name);

	int pipefd[2];
	if (pipe(pipefd) != 0) return 1;

	pid_t pid = fork();
	if (pid < 0) {
		close(pipefd[0]);
		close(pipefd[1]);
		return 1;
	}
	if (pid == 0) {
		close(pipefd[0]);
		dup2(pipefd[1], STDOUT_FILENO);
		close(pipefd[1]);
		char **new_argv = calloc((size_t)argc + 1, sizeof *new_argv);
		if (!new_argv) _exit(127);
		new_argv[0] = path;
		for (int i = 1; i < argc; i++)
			new_argv[i] = argv[i];
		execv(path, new_argv);
		_exit(127);
	}
	close(pipefd[1]);

	struct grabit_buf out = {0};
	char chunk[4096];
	ssize_t r;
	while ((r = read(pipefd[0], chunk, sizeof chunk)) > 0) {
		if (grabit_buf_putn(&out, chunk, (size_t)r) != 0) break;
	}
	close(pipefd[0]);
	int status = 0;
	(void)grabit_waitpid_intr(pid, &status);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		if (out.data) fputs(out.data, stderr);
		grabit_buf_free(&out);
		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}

	const char *last = last_line(&out);
	if (!*last) {
		log_error("plugin: %s produced no output to pin", name);
		grabit_buf_free(&out);
		return 1;
	}

	const char *self = getenv("GRABIT_BIN");
	if (!self || !*self) {
		log_error("plugin: GRABIT_BIN not set");
		grabit_buf_free(&out);
		return 1;
	}
	char *const pin_argv[] = {(char *)self, "--pin", "-f", (char *)last, NULL};
	execv(self, pin_argv);
	log_error("plugin: exec --pin: %s", strerror(errno));
	grabit_buf_free(&out);
	return 1;
}
