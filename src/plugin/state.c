// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/state.h"

#include "plugin/plugin.h"
#include "util.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int plugin_state_write(const char *plugin_dir, const char *kind,
					   const char *url, const char *sha256) {
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/.source", plugin_dir) != 0) return -1;
	char *content = NULL;
	if (grabit_xasprintf(&content, "%s\n%s\n%s\n", kind, url ? url : "",
						 sha256 ? sha256 : "") != 0) {
		free(path);
		return -1;
	}
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	free(path);
	int rc = -1;
	if (fd >= 0) {
		size_t len = strlen(content);
		ssize_t w = write(fd, content, len);
		close(fd);
		if ((size_t)w == len) rc = 0;
	}
	free(content);
	return rc;
}

int plugin_state_read(const char *plugin_dir,
					  char *kind, size_t kind_cap,
					  char *url, size_t url_cap,
					  char *sha, size_t sha_cap) {
	if (kind_cap) kind[0] = '\0';
	if (url_cap) url[0] = '\0';
	if (sha_cap) sha[0] = '\0';

	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/.source", plugin_dir) != 0) return -1;
	FILE *f = fopen(path, "r");
	free(path);
	if (!f) return -1;

	char *lines[3] = {kind, url, sha};
	size_t caps[3] = {kind_cap, url_cap, sha_cap};
	for (int i = 0; i < 3; i++) {
		if (caps[i] == 0) continue;
		if (!fgets(lines[i], (int)caps[i], f)) break;
		char *nl = strchr(lines[i], '\n');
		if (nl) *nl = '\0';
	}
	fclose(f);
	return kind[0] ? 0 : -1;
}

int plugin_touch_check(const char *plugin_dir) {
	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/.last_check", plugin_dir) != 0) return -1;
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	free(path);
	if (fd < 0) return -1;
	close(fd);
	return 0;
}
