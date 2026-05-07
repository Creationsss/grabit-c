// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"
#include "paths.h"
#include "plugin/fetch.h"
#include "plugin/lock.h"
#include "plugin/spawn.h"
#include "plugin/state.h"
#include "util.h"
#include "vendor/sha256/sha256.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int git_clone(const char *url, const char *dst) {
	char *const argv[] = {"git", "clone", "--depth", "1", (char *)url, (char *)dst, NULL};
	return plugin_run_in(NULL, argv);
}

static int run_shell(const char *cwd, const char *cmd) {
	char *const argv[] = {"/bin/sh", "-c", (char *)cmd, NULL};
	return plugin_run_in(cwd, argv);
}

static const char *url_basename(const char *url) {
	const char *s = strrchr(url, '/');
	return s ? s + 1 : url;
}

static char *plugin_name_from_url(const char *url) {
	const char *base = url_basename(url);
	if (!base[0]) return NULL;
	char *out = strdup(base);
	if (!out) return NULL;
	size_t n = strlen(out);
	if (n >= 4 && strcmp(out + n - 4, ".git") == 0) out[n - 4] = '\0';
	if (strncmp(out, "grabit-", 7) == 0) memmove(out, out + 7, strlen(out + 7) + 1);
	for (char *p = out; *p; p++) {
		bool ok = (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
				  *p == '_' || *p == '-';
		if (!ok) *p = '_';
	}
	if (!out[0]) {
		free(out);
		return NULL;
	}
	return out;
}

static int symlink_force(const char *target, const char *link_path) {
	unlink(link_path);
	return symlink(target, link_path);
}

static int verify_sha256(const char *path, const char *expect_hex) {
	if (!expect_hex || !*expect_hex) return 0;
	char actual[SHA256_HEX_SIZE];
	if (plugin_sha256_file(path, actual) != 0) {
		log_error("plugin: cannot hash %s", path);
		return -1;
	}
	if (!plugin_sha256_equal(expect_hex, actual)) {
		log_error("plugin: sha256 mismatch on %s", path);
		log_error("  expected: %s", expect_hex);
		log_error("  actual:   %s", actual);
		return -1;
	}
	return 0;
}

int plugin_install_git(const char *url) {
	if (!url || !*url) return -1;

	int lock_fd = plugin_lock_acquire();
	if (lock_fd < 0) return -1;

	char *tmp_name = plugin_name_from_url(url);
	if (!tmp_name) {
		log_error("plugin: cannot derive temp dir name from %s", url);
		plugin_lock_release(lock_fd);
		return -1;
	}

	const char *root = plugin_dir_path();
	const char *bin = plugin_bin_dir_path();
	if (!root[0] || paths_mkdir_p(root) != 0 || paths_mkdir_p(bin) != 0) {
		log_error("plugin: cannot create %s", root);
		free(tmp_name);
		plugin_lock_release(lock_fd);
		return -1;
	}

	char *plugin_dir = NULL;
	int ret = -1;
	if (grabit_xasprintf(&plugin_dir, "%s/.tmp.%s.%d", root, tmp_name, (int)getpid()) != 0) {
		free(tmp_name);
		goto release;
	}
	free(tmp_name);

	struct stat st;
	if (stat(plugin_dir, &st) == 0) {
		char *const argv[] = {"rm", "-rf", plugin_dir, NULL};
		(void)plugin_run_in(NULL, argv);
	}

	log_info("plugin: cloning %s ...", url);
	if (git_clone(url, plugin_dir) != 0) {
		log_error("plugin: git clone failed");
		goto fail_clone;
	}

	char *manifest_path = NULL;
	if (grabit_xasprintf(&manifest_path, "%s/manifest.toml", plugin_dir) != 0) goto fail_clone;
	struct plugin_manifest m;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) {
		log_error("plugin: clone has no valid manifest.toml");
		free(manifest_path);
		goto fail_clone;
	}
	free(manifest_path);

	if (!plugin_name_is_valid(m.name)) {
		log_error("plugin: invalid manifest name `%s` (must be [a-z0-9_-]+)", m.name);
		goto fail_manifest;
	}

	char *final_dir = NULL;
	if (grabit_xasprintf(&final_dir, "%s/%s", root, m.name) != 0) goto fail_manifest;
	if (stat(final_dir, &st) == 0) {
		char src_kind[32], src_url[2048], src_sha[65];
		plugin_state_read(final_dir, src_kind, sizeof src_kind,
						  src_url, sizeof src_url, src_sha, sizeof src_sha);
		if (src_url[0] && strcmp(src_url, url) == 0) {
			log_info("plugin: %s already installed from %s", m.name, url);
			ret = 0;
		} else {
			log_error("plugin: %s already installed (from %s); `grabit plugin remove %s` first",
					  m.name, src_url[0] ? src_url : "unknown source", m.name);
		}
		free(final_dir);
		goto fail_manifest;
	}
	if (rename(plugin_dir, final_dir) != 0) {
		log_error("plugin: rename %s -> %s: %s", plugin_dir, final_dir, strerror(errno));
		free(final_dir);
		goto fail_manifest;
	}
	free(plugin_dir);
	plugin_dir = final_dir;

	if (m.kind == PLUGIN_KIND_BUILD) {
		log_info("plugin: building (%s) ...", m.build_cmd);
		if (run_shell(plugin_dir, m.build_cmd) != 0) {
			log_error("plugin: build failed");
			goto fail_manifest;
		}
	}

	char *binary_path = NULL;
	if (m.kind == PLUGIN_KIND_BUILD) {
		if (grabit_xasprintf(&binary_path, "%s/%s", plugin_dir, m.build_binary) != 0)
			goto fail_manifest;
	} else {
		if (grabit_xasprintf(&binary_path, "%s/%s", plugin_dir, m.name) != 0)
			goto fail_manifest;
		if (plugin_fetch_url(m.prebuilt_url, binary_path, 0) != PLUGIN_FETCH_OK) {
			free(binary_path);
			goto fail_manifest;
		}
		if (verify_sha256(binary_path, m.prebuilt_sha256) != 0) {
			unlink(binary_path);
			free(binary_path);
			goto fail_manifest;
		}
		chmod(binary_path, 0755);
	}

	if (access(binary_path, X_OK) != 0) {
		log_error("plugin: binary not executable: %s", binary_path);
		free(binary_path);
		goto fail_manifest;
	}

	char *link_path = NULL;
	if (grabit_xasprintf(&link_path, "%s/grabit-%s", bin, m.name) != 0) {
		free(binary_path);
		goto fail_manifest;
	}
	if (symlink_force(binary_path, link_path) != 0) {
		log_error("plugin: symlink %s -> %s: %s", link_path, binary_path, strerror(errno));
		free(binary_path);
		free(link_path);
		goto fail_manifest;
	}
	free(binary_path);
	free(link_path);

	if (m.kind == PLUGIN_KIND_PREBUILT) {
		plugin_state_write(plugin_dir, "prebuilt", m.prebuilt_url, m.prebuilt_sha256);
	} else {
		plugin_state_write(plugin_dir, "git", url, NULL);
	}
	plugin_touch_check(plugin_dir);

	log_info("plugin: installed %s -> grabit %s", m.name, m.name);
	plugin_manifest_free(&m);
	free(plugin_dir);
	plugin_lock_release(lock_fd);
	return 0;

fail_manifest:
	plugin_manifest_free(&m);
fail_clone:
	if (plugin_dir) {
		char *const argv[] = {"rm", "-rf", plugin_dir, NULL};
		(void)plugin_run_in(NULL, argv);
	}
	free(plugin_dir);
release:
	plugin_lock_release(lock_fd);
	return ret;
}

int plugin_remove(const char *name) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return -1;
	}

	int lock_fd = plugin_lock_acquire();
	if (lock_fd < 0) return -1;

	const char *root = plugin_dir_path();
	const char *bin = plugin_bin_dir_path();
	int ret = -1;
	char *plugin_dir = NULL;
	char *link_path = NULL;

	if (!root[0]) goto out;
	if (grabit_xasprintf(&plugin_dir, "%s/%s", root, name) != 0) goto out;
	struct stat st;
	if (stat(plugin_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		log_info("plugin: %s is not installed", name);
		ret = 0;
		goto out;
	}
	if (grabit_xasprintf(&link_path, "%s/grabit-%s", bin, name) != 0) goto out;
	unlink(link_path);

	char *const argv[] = {"rm", "-rf", plugin_dir, NULL};
	if (plugin_run_in(NULL, argv) != 0) {
		log_error("plugin: rm -rf failed");
		goto out;
	}
	log_info("plugin: removed %s", name);
	ret = 0;
out:
	free(plugin_dir);
	free(link_path);
	plugin_lock_release(lock_fd);
	return ret;
}
