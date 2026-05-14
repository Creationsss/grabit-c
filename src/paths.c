// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "paths.h"

#include "config.h"
#include "hyprland.h"
#include "log.h"
#include "template.h"
#include "util.h"

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
	const char *dir = dirname(path_copy);

	char tmpl[4096];
	int n = snprintf(tmpl, sizeof tmpl, "%s/.grabit.XXXXXX", dir);
	if (n < 0 || (size_t)n >= sizeof tmpl) {
		free(path_copy);
		errno = ENAMETOOLONG;
		return -1;
	}

	int fd = mkstemp(tmpl);
	if (fd < 0) {
		free(path_copy);
		return -1;
	}

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

	if (fsync_dir(dir) != 0) {
		log_warn("dirsync(%s): %s", path, strerror(errno));
	}
	free(path_copy);
	return 0;

fail: {
	int saved = errno;
	if (fd >= 0) close(fd);
	unlink(tmpl);
	free(path_copy);
	errno = saved;
	return -1;
}
}

static char *resolve_dir(struct config *cfg, enum paths_dest dest) {
	if (dest == PATHS_DEST_TEMP) return strdup("/tmp");

	const char *d = config_get(cfg, "save_dir");
	if (d && d[0]) return strdup(d);

	const char *home = getenv("HOME");
	const char *fallback = "Pictures";
	if (dest == PATHS_DEST_VIDEOS) {
		const char *xdg = getenv("XDG_VIDEOS_DIR");
		if (xdg && xdg[0]) return strdup(xdg);
		fallback = "Videos";
	}
	if (home && home[0]) {
		char *out = NULL;
		(void)grabit_xasprintf(&out, "%s/%s", home, fallback);
		if (out) return out;
	}
	return strdup("/tmp");
}

char *paths_build_output(struct config *cfg, const char *cli_template,
						 const char *extension, enum paths_dest dest) {
	const char *tpl = cli_template;
	if (!tpl) tpl = config_get(cfg, "filename");
	if (!tpl) {
		const char *preset = config_get(cfg, "filename_preset");
		tpl = template_for_preset(preset);
	}

	char *win_class = NULL, *win_title = NULL;
	(void)grabit_hyprland_active_window(&win_class, &win_title);
	struct template_ctx ctx = {
		.window_class = win_class,
		.window_title = win_title,
	};
	char *raw = template_expand(tpl, &ctx);
	free(win_class);
	free(win_title);
	if (!raw) return NULL;
	char *clean = template_sanitize(raw);
	free(raw);
	if (!clean || !clean[0]) {
		free(clean);
		return NULL;
	}

	char *dir = resolve_dir(cfg, dest);
	if (!dir) {
		free(clean);
		return NULL;
	}
	if (paths_mkdir_p(dir) != 0) {
		log_error("mkdir %s: %s", dir, strerror(errno));
		free(dir);
		free(clean);
		return NULL;
	}

	static const char *const KNOWN_EXT[] = {
		".png",
		".jpg",
		".jpeg",
		".webp",
		NULL,
	};
	for (size_t i = 0; KNOWN_EXT[i]; i++) {
		size_t el = strlen(KNOWN_EXT[i]);
		size_t cl = strlen(clean);
		if (cl > el && strcasecmp(clean + cl - el, KNOWN_EXT[i]) == 0) {
			clean[cl - el] = '\0';
			break;
		}
	}

	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/%s%s", dir, clean, extension) != 0) {
		free(dir);
		free(clean);
		return NULL;
	}
	if (dest == PATHS_DEST_PICTURES && access(path, F_OK) == 0) {
		for (int n = 1; n < 10000; n++) {
			char *candidate = NULL;
			if (grabit_xasprintf(&candidate, "%s/%s-%d%s",
								 dir, clean, n, extension) != 0) {
				break;
			}
			if (access(candidate, F_OK) != 0) {
				free(path);
				path = candidate;
				break;
			}
			free(candidate);
		}
	}
	free(dir);
	free(clean);
	return path;
}
