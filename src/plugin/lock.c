// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/lock.h"

#include "log.h"
#include "paths.h"
#include "plugin/plugin.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

int plugin_lock_acquire(void) {
	const char *root = plugin_dir_path();
	if (!root[0]) return -1;
	if (paths_mkdir_p(root) != 0) return -1;

	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/.lock", root) != 0) return -1;
	int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
	if (fd < 0) {
		log_error("plugin: cannot open lock file %s: %s", path, strerror(errno));
		free(path);
		return -1;
	}
	if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
		log_error("plugin: another grabit plugin operation is in progress");
		close(fd);
		free(path);
		return -1;
	}
	free(path);
	return fd;
}

void plugin_lock_release(int fd) {
	if (fd < 0) return;
	flock(fd, LOCK_UN);
	close(fd);
}
