// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/plugin.h"

#include "log.h"
#include "vendor/tomlc99/toml.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFAULT_UPDATE_HOURS 24
#define DEFAULT_BRANCH "main"

void plugin_manifest_free(struct plugin_manifest *m) {
	if (!m) return;
	free(m->name);
	free(m->description);
	free(m->homepage);
	free(m->build_cmd);
	free(m->build_binary);
	free(m->prebuilt_url);
	free(m->prebuilt_sha256);
	free(m->branch);
	for (size_t i = 0; i < m->n_actions; i++) {
		free(m->actions[i].name);
		free(m->actions[i].description);
	}
	free(m->actions);
	memset(m, 0, sizeof *m);
}

static void parse_actions(toml_table_t *root, struct plugin_manifest *out) {
	toml_table_t *actions = toml_table_in(root, "actions");
	if (!actions) return;
	int n = 0;
	while (toml_key_in(actions, n) != NULL) n++;
	if (n <= 0) return;
	out->actions = calloc((size_t)n, sizeof *out->actions);
	if (!out->actions) return;
	for (int i = 0; i < n; i++) {
		const char *key = toml_key_in(actions, i);
		toml_table_t *t = toml_table_in(actions, key);
		out->actions[i].name = strdup(key);
		if (t) {
			toml_datum_t d = toml_string_in(t, "description");
			if (d.ok) out->actions[i].description = d.u.s;
		}
	}
	out->n_actions = (size_t)n;
}

static char *take_string(toml_table_t *t, const char *key) {
	toml_datum_t d = toml_string_in(t, key);
	if (!d.ok) return NULL;
	return d.u.s;
}

static int take_int(toml_table_t *t, const char *key, int dflt) {
	toml_datum_t d = toml_int_in(t, key);
	if (!d.ok) return dflt;
	return (int)d.u.i;
}

int plugin_manifest_parse_file(const char *path, struct plugin_manifest *out) {
	if (!path || !out) return -1;
	memset(out, 0, sizeof *out);
	out->update_check_hours = DEFAULT_UPDATE_HOURS;

	FILE *f = fopen(path, "r");
	if (!f) {
		log_error("plugin: open %s: %s", path, strerror(errno));
		return -1;
	}
	char errbuf[256];
	toml_table_t *root = toml_parse_file(f, errbuf, sizeof errbuf);
	fclose(f);
	if (!root) {
		log_error("plugin: parse %s: %s", path, errbuf);
		return -1;
	}

	out->name = take_string(root, "name");
	out->description = take_string(root, "description");
	out->homepage = take_string(root, "homepage");

	toml_table_t *build = toml_table_in(root, "build");
	toml_table_t *prebuilt = toml_table_in(root, "prebuilt");
	if (prebuilt) {
		out->kind = PLUGIN_KIND_PREBUILT;
		out->prebuilt_url = take_string(prebuilt, "url");
		out->prebuilt_sha256 = take_string(prebuilt, "sha256");
	} else if (build) {
		out->kind = PLUGIN_KIND_BUILD;
		out->build_cmd = take_string(build, "cmd");
		out->build_binary = take_string(build, "binary");
	}

	toml_table_t *update = toml_table_in(root, "update");
	if (update) {
		out->update_check_hours = take_int(update, "check_every_hours", DEFAULT_UPDATE_HOURS);
		out->branch = take_string(update, "branch");
	}
	if (!out->branch) out->branch = strdup(DEFAULT_BRANCH);

	toml_table_t *capture = toml_table_in(root, "capture");
	if (capture) {
		toml_datum_t d = toml_bool_in(capture, "auto");
		if (d.ok) out->capture_auto = d.u.b;
	}

	parse_actions(root, out);

	toml_free(root);

	if (!out->name || !out->name[0]) {
		log_error("plugin: manifest missing required `name`");
		plugin_manifest_free(out);
		return -1;
	}
	if (out->kind == PLUGIN_KIND_BUILD &&
		(!out->build_cmd || !out->build_binary)) {
		log_error("plugin: [build] requires both `cmd` and `binary`");
		plugin_manifest_free(out);
		return -1;
	}
	if (out->kind == PLUGIN_KIND_PREBUILT && !out->prebuilt_url) {
		log_error("plugin: [prebuilt] requires `url`");
		plugin_manifest_free(out);
		return -1;
	}
	if (!out->build_cmd && !out->prebuilt_url) {
		log_error("plugin: manifest must define [build] or [prebuilt]");
		plugin_manifest_free(out);
		return -1;
	}
	return 0;
}
