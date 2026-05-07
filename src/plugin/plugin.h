// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_H
#define GRABIT_PLUGIN_H

#include <stdbool.h>
#include <stddef.h>

enum plugin_kind {
	PLUGIN_KIND_BUILD,
	PLUGIN_KIND_PREBUILT,
};

struct plugin_action {
	char *name;
	char *description;
};

struct plugin_manifest {
	char *name;
	char *description;
	char *homepage;
	enum plugin_kind kind;
	char *build_cmd;
	char *build_binary;
	char *prebuilt_url;
	char *prebuilt_sha256;
	int update_check_hours;
	char *branch;
	bool capture_auto;
	struct plugin_action *actions;
	size_t n_actions;
};

void plugin_manifest_free(struct plugin_manifest *m);
int plugin_manifest_parse_file(const char *path, struct plugin_manifest *out);

const char *plugin_dir_path(void);
const char *plugin_bin_dir_path(void);

bool plugin_name_is_valid(const char *name);
int plugin_resolve(const char *name, char *path_out, size_t cap);
bool plugin_is_installed(const char *name);

int plugin_install_git(const char *url);
int plugin_remove(const char *name);
int plugin_update(const char *name);
int plugin_update_all(void);
int plugin_touch_check(const char *plugin_dir);

void plugin_maybe_auto_update(const char *name);

int cmd_plugin(int argc, char **argv);

#endif
