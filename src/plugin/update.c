// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"
#include "plugin/fetch.h"
#include "plugin/lock.h"
#include "plugin/spawn.h"
#include "util.h"
#include "vendor/sha256/sha256.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static bool stale(const char *path, int hours) {
	if (hours <= 0) return false;
	struct stat st;
	if (stat(path, &st) != 0) return true;
	time_t now = time(NULL);
	return (now - st.st_mtime) > (time_t)hours * 3600;
}

static int read_source_lines(const char *path, char *kind, size_t kind_cap,
							 char *url, size_t url_cap, char *sha, size_t sha_cap) {
	FILE *f = fopen(path, "r");
	if (!f) return -1;
	char *lines[3] = {kind, url, sha};
	size_t caps[3] = {kind_cap, url_cap, sha_cap};
	for (int i = 0; i < 3; i++) {
		lines[i][0] = '\0';
		if (!fgets(lines[i], (int)caps[i], f)) break;
		char *nl = strchr(lines[i], '\n');
		if (nl) *nl = '\0';
	}
	fclose(f);
	return kind[0] ? 0 : -1;
}

static int update_prebuilt(const char *plugin_dir, const char *name,
						   const char *url, const char *sha) {
	if (!url || !*url) {
		log_error("plugin: %s missing prebuilt url in .source", name);
		return -1;
	}
	char *binary_path = NULL;
	char *tmp_path = NULL;
	int rc = -1;
	if (grabit_xasprintf(&binary_path, "%s/%s", plugin_dir, name) != 0) goto out;
	if (grabit_xasprintf(&tmp_path, "%s.new", binary_path) != 0) goto out;

	struct stat st;
	time_t since = (stat(binary_path, &st) == 0) ? st.st_mtime : 0;
	log_info("plugin: checking %s for updates ...", name);
	enum plugin_fetch_result res = plugin_fetch_url(url, tmp_path, since);
	if (res == PLUGIN_FETCH_NOT_MODIFIED) {
		log_info("plugin: %s is up to date", name);
		rc = 0;
		goto out;
	}
	if (res != PLUGIN_FETCH_OK) goto out;

	if (sha && *sha) {
		char actual[SHA256_HEX_SIZE];
		if (plugin_sha256_file(tmp_path, actual) != 0 ||
			!plugin_sha256_equal(sha, actual)) {
			log_error("plugin: sha256 mismatch on update; keeping current binary");
			unlink(tmp_path);
			goto out;
		}
	}
	chmod(tmp_path, 0755);
	if (rename(tmp_path, binary_path) != 0) {
		log_error("plugin: rename %s -> %s: %s", tmp_path, binary_path, strerror(errno));
		unlink(tmp_path);
		goto out;
	}
	log_info("plugin: %s updated", name);
	rc = 0;
out:
	free(binary_path);
	free(tmp_path);
	return rc;
}

int plugin_update(const char *name) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return -1;
	}
	int lock_fd = plugin_lock_acquire();
	if (lock_fd < 0) return -1;

	char *plugin_dir = NULL;
	char *manifest_path = NULL;
	char *source_path = NULL;
	struct plugin_manifest m = {0};
	int rc = -1;

	if (grabit_xasprintf(&plugin_dir, "%s/%s", plugin_dir_path(), name) != 0) goto out;
	if (grabit_xasprintf(&manifest_path, "%s/manifest.toml", plugin_dir) != 0) goto out;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) goto out;

	if (grabit_xasprintf(&source_path, "%s/.source", plugin_dir) != 0) goto out;
	char source_kind[32] = {0};
	char source_url[2048] = {0};
	char source_sha[SHA256_HEX_SIZE] = {0};
	if (read_source_lines(source_path, source_kind, sizeof source_kind,
						  source_url, sizeof source_url,
						  source_sha, sizeof source_sha) != 0) {
		strncpy(source_kind, "git", sizeof source_kind - 1);
	}

	if (strcmp(source_kind, "git") == 0) {
		log_info("plugin: updating %s ...", name);
		char *const fetch[] = {"git", "-C", plugin_dir, "fetch", "--quiet", "--depth", "1", NULL};
		if (plugin_run_in(NULL, fetch) != 0) goto out;
		char *const reset[] = {"git", "-C", plugin_dir, "reset", "--hard", "FETCH_HEAD", NULL};
		if (plugin_run_in(NULL, reset) != 0) goto out;
		if (m.kind == PLUGIN_KIND_BUILD) {
			char *const sh[] = {"/bin/sh", "-c", m.build_cmd, NULL};
			if (plugin_run_in(plugin_dir, sh) != 0) goto out;
		}
		rc = 0;
	} else if (strcmp(source_kind, "prebuilt") == 0) {
		const char *url = source_url[0] ? source_url : m.prebuilt_url;
		const char *sha = source_sha[0] ? source_sha : m.prebuilt_sha256;
		rc = update_prebuilt(plugin_dir, name, url, sha);
	} else {
		log_error("plugin: unknown source kind `%s`", source_kind);
	}

	plugin_touch_check(plugin_dir);
out:
	plugin_manifest_free(&m);
	free(plugin_dir);
	free(manifest_path);
	free(source_path);
	plugin_lock_release(lock_fd);
	return rc;
}

int plugin_update_all(void) {
	const char *root = plugin_dir_path();
	if (!root[0]) return -1;
	DIR *d = opendir(root);
	if (!d) return 0;
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		if (!plugin_name_is_valid(e->d_name)) continue;
		char *path = NULL;
		if (grabit_xasprintf(&path, "%s/%s", root, e->d_name) != 0) continue;
		struct stat st;
		bool is_dir = (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
		free(path);
		if (!is_dir) continue;
		if (plugin_update(e->d_name) == 0) n++;
	}
	closedir(d);
	log_info("plugin: updated %d plugin(s)", n);
	return 0;
}

void plugin_maybe_auto_update(const char *name) {
	if (!plugin_name_is_valid(name)) return;
	char *plugin_dir = NULL;
	char *manifest_path = NULL;
	char *check_path = NULL;
	struct plugin_manifest m = {0};
	bool should_spawn = false;

	if (grabit_xasprintf(&plugin_dir, "%s/%s", plugin_dir_path(), name) != 0) goto out;
	if (grabit_xasprintf(&manifest_path, "%s/manifest.toml", plugin_dir) != 0) goto out;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) goto out;
	if (m.update_check_hours <= 0) goto out;
	if (grabit_xasprintf(&check_path, "%s/.last_check", plugin_dir) != 0) goto out;
	if (!stale(check_path, m.update_check_hours)) goto out;
	plugin_touch_check(plugin_dir);
	should_spawn = true;
out:
	plugin_manifest_free(&m);
	free(plugin_dir);
	free(manifest_path);
	free(check_path);
	if (!should_spawn) return;

	pid_t pid = fork();
	if (pid < 0) return;
	if (pid != 0) return;

	pid_t gp = fork();
	if (gp < 0) _exit(0);
	if (gp != 0) _exit(0);

	setsid();
	signal(SIGHUP, SIG_IGN);

	char log_path[1024];
	snprintf(log_path, sizeof log_path, "%s/%s/.update.log", plugin_dir_path(), name);
	int logfd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (logfd >= 0) {
		dup2(logfd, STDOUT_FILENO);
		dup2(logfd, STDERR_FILENO);
		if (logfd > STDERR_FILENO) close(logfd);
	}
	int devnull = open("/dev/null", O_RDONLY);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		if (devnull > STDERR_FILENO) close(devnull);
	}

	char self[1024];
	ssize_t sn = readlink("/proc/self/exe", self, sizeof self - 1);
	if (sn <= 0) _exit(127);
	self[sn] = '\0';

	char *const argv[] = {self, "plugin", "update", (char *)name, NULL};
	execv(self, argv);
	_exit(127);
}
