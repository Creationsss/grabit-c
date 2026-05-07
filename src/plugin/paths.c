// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "paths.h"

#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>

#define PLUGIN_PATH_MAX 1024

static char g_plugin_dir[PLUGIN_PATH_MAX];
static char g_plugin_bin[PLUGIN_PATH_MAX];
static bool g_init;

static void init_paths(void) {
	if (g_init) return;
	const char *cfg = paths_config_dir();
	if (cfg && cfg[0]) {
		snprintf(g_plugin_dir, sizeof g_plugin_dir, "%s/plugins", cfg);
		snprintf(g_plugin_bin, sizeof g_plugin_bin, "%s/plugins/.bin", cfg);
	} else {
		g_plugin_dir[0] = '\0';
		g_plugin_bin[0] = '\0';
	}
	g_init = true;
}

bool plugin_name_is_valid(const char *name) {
	if (!name || !*name) return false;
	for (const char *p = name; *p; p++) {
		char c = *p;
		bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
				  c == '_' || c == '-';
		if (!ok) return false;
	}
	return true;
}

const char *plugin_dir_path(void) {
	init_paths();
	return g_plugin_dir;
}

const char *plugin_bin_dir_path(void) {
	init_paths();
	return g_plugin_bin;
}

int plugin_resolve(const char *name, char *path_out, size_t cap) {
	if (!name || !*name || !path_out || cap == 0) return -1;
	const char *bin = plugin_bin_dir_path();
	if (!bin[0]) return -1;
	int n = snprintf(path_out, cap, "%s/grabit-%s", bin, name);
	if (n <= 0 || (size_t)n >= cap) return -1;
	if (access(path_out, X_OK) != 0) return -1;
	return 0;
}

bool plugin_is_installed(const char *name) {
	char path[PLUGIN_PATH_MAX];
	return plugin_resolve(name, path, sizeof path) == 0;
}
