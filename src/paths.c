// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "paths.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static char *g_config_dir;
static char *g_config_file;

const char *paths_config_dir(void) {
	if (g_config_dir) return g_config_dir;

	const char *xdg = getenv("XDG_CONFIG_HOME");
	int rc;
	if (xdg && xdg[0] == '/') {
		rc = grabit_xasprintf(&g_config_dir, "%s/grabit", xdg);
	} else {
		const char *home = getenv("HOME");
		if (!home || home[0] != '/') {
			die("HOME is not set");
		}
		rc = grabit_xasprintf(&g_config_dir, "%s/.config/grabit", home);
	}
	if (rc != 0 || !g_config_dir) die("oom: paths_config_dir");
	return g_config_dir;
}

const char *paths_config_file(void) {
	if (g_config_file) return g_config_file;
	if (grabit_xasprintf(&g_config_file, "%s/config.toml", paths_config_dir()) != 0) {
		die("oom: paths_config_file");
	}
	return g_config_file;
}

int paths_mkdir_p(const char *path) {
	if (!path || !path[0]) {
		errno = EINVAL;
		return -1;
	}
	char *copy = strdup(path);
	if (!copy) return -1;

	// skip leading slash on absolute paths so we don't mkdir(""); relative paths start from the first separator.
	for (char *p = copy[0] == '/' ? copy + 1 : copy; *p; p++) {
		if (*p != '/') continue;
		*p = '\0';
		if (mkdir(copy, 0755) != 0 && errno != EEXIST) {
			int saved = errno;
			free(copy);
			errno = saved;
			return -1;
		}
		*p = '/';
	}
	int rc = (mkdir(copy, 0755) != 0 && errno != EEXIST) ? -1 : 0;
	free(copy);
	return rc;
}

static int fsync_dir(const char *path) {
	int fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0) return -1;
	int rc = fsync(fd);
	close(fd);
	return rc;
}

int paths_atomic_write(const char *path, const void *buf, size_t len) {
	char *path_copy = strdup(path);
	if (!path_copy) return -1;
	char *dir = dirname(path_copy);

	char tmpl[4096];
	int n = snprintf(tmpl, sizeof tmpl, "%s/.grabit.XXXXXX", dir);
	free(path_copy);
	if (n < 0 || (size_t)n >= sizeof tmpl) {
		errno = ENAMETOOLONG;
		return -1;
	}

	int fd = mkstemp(tmpl);
	if (fd < 0) return -1;

	const char *p = buf;
	size_t left = len;
	while (left > 0) {
		ssize_t w = write(fd, p, left);
		if (w < 0) {
			if (errno == EINTR) continue;
			goto fail;
		}
		p += w;
		left -= (size_t)w;
	}

	if (fchmod(fd, 0600) != 0) goto fail;
	if (fsync(fd) != 0) goto fail;
	if (close(fd) != 0) {
		fd = -1;
		goto fail;
	}
	fd = -1;

	if (rename(tmpl, path) != 0) goto fail;

	char *path_copy2 = strdup(path);
	if (path_copy2) {
		if (fsync_dir(dirname(path_copy2)) != 0) {
			log_warn("dirsync(%s): %s", path, strerror(errno));
		}
		free(path_copy2);
	}
	return 0;

fail: {
	int saved = errno;
	if (fd >= 0) close(fd);
	unlink(tmpl);
	errno = saved;
	return -1;
}
}
