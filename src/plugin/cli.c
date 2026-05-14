// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int usage(void) {
	log_error("usage: grabit plugin <install|list|show|update|remove> [args]");
	return 1;
}

static int help(void) {
	puts("usage: grabit plugin <subcommand> [args]");
	puts("");
	puts("  install <git-url>  clone, build (or fetch prebuilt), install (alias: add)");
	puts("  list               list installed plugins (alias: ls)");
	puts("  show <name>        print parsed manifest");
	puts("  update [<name>]    update one plugin (or all if omitted)");
	puts("  remove <name>      uninstall a plugin (alias: rm)");
	return 0;
}

static int do_list(void) {
	const char *root = plugin_dir_path();
	if (!root[0]) return 1;
	DIR *d = opendir(root);
	if (!d) {
		log_info("no plugins installed");
		return 0;
	}
	int n = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		char path[1024];
		snprintf(path, sizeof path, "%s/%s", root, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
		puts(e->d_name);
		n++;
	}
	closedir(d);
	if (n == 0) log_info("no plugins installed in %s", root);
	return 0;
}

static int do_show(const char *name) {
	if (!plugin_name_is_valid(name)) {
		log_error("plugin: invalid name `%s`", name ? name : "");
		return 1;
	}
	char manifest_path[1024];
	int n = snprintf(manifest_path, sizeof manifest_path, "%s/%s/manifest.toml",
					 plugin_dir_path(), name);
	if (n <= 0 || (size_t)n >= sizeof manifest_path) return 1;
	struct plugin_manifest m;
	if (plugin_manifest_parse_file(manifest_path, &m) != 0) return 1;

	printf("name:        %s\n", m.name);
	if (m.description) printf("description: %s\n", m.description);
	if (m.homepage) printf("homepage:    %s\n", m.homepage);
	printf("kind:        %s\n", m.kind == PLUGIN_KIND_BUILD ? "build" : "prebuilt");
	if (m.kind == PLUGIN_KIND_BUILD) {
		printf("build cmd:   %s\n", m.build_cmd);
		printf("binary:      %s\n", m.build_binary);
	} else {
		printf("prebuilt:    %s\n", m.prebuilt_url);
		if (m.prebuilt_sha256) printf("sha256:      %s\n", m.prebuilt_sha256);
	}
	printf("auto-update: %d hour(s)\n", m.update_check_hours);
	if (m.branch) printf("branch:      %s\n", m.branch);
	printf("auto-capture: %s\n", m.capture_auto ? "yes" : "no");
	for (size_t i = 0; i < m.n_actions; i++) {
		printf("action:      %s%s%s\n", m.actions[i].name,
			   m.actions[i].description ? " — " : "",
			   m.actions[i].description ? m.actions[i].description : "");
	}

	plugin_manifest_free(&m);
	return 0;
}

int cmd_plugin(int argc, char **argv) {
	if (argc < 1) return usage();
	const char *sub = argv[0];
	if (strcmp(sub, "--help") == 0 || strcmp(sub, "-h") == 0) {
		return help();
	}
	if (strcmp(sub, "install") == 0 || strcmp(sub, "add") == 0) {
		if (argc != 2) {
			log_error("usage: grabit plugin install <git-url>");
			return 1;
		}
		return plugin_install_git(argv[1]) == 0 ? 0 : 1;
	}
	if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) return do_list();
	if (strcmp(sub, "show") == 0) {
		if (argc != 2) return usage();
		return do_show(argv[1]);
	}
	if (strcmp(sub, "update") == 0) {
		if (argc == 1) return plugin_update_all() == 0 ? 0 : 1;
		if (argc == 2) return plugin_update(argv[1]) == 0 ? 0 : 1;
		return usage();
	}
	if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0) {
		if (argc != 2) return usage();
		return plugin_remove(argv[1]) == 0 ? 0 : 1;
	}
	return usage();
}
