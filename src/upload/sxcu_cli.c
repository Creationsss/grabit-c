// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "log.h"
#include "upload/sxcu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int usage(void) {
	log_error("usage: grabit sxcu <add|list|remove|show> [args]");
	return 1;
}

static int do_list(void) {
	char **names = NULL;
	size_t n = 0;
	if (sxcu_dir_list(&names, &n) != 0) {
		log_error("sxcu: cannot read %s", sxcu_dir_path());
		return 1;
	}
	if (n == 0) {
		log_info("no .sxcu uploaders registered in %s", sxcu_dir_path());
	} else {
		for (size_t i = 0; i < n; i++)
			puts(names[i]);
	}
	for (size_t i = 0; i < n; i++)
		free(names[i]);
	free(names);
	return 0;
}

static int do_show(const char *name) {
	struct sxcu_uploader u = {0};
	if (sxcu_dir_lookup(name, &u) != 0) {
		log_error("sxcu: %s not found in %s", name, sxcu_dir_path());
		return 1;
	}
	printf("name:        %s\n", u.name ? u.name : "");
	printf("url:         %s\n", u.request_url ? u.request_url : "");
	printf("method:      %s\n", sxcu_method_str(u.method));
	printf("body:        %s\n", sxcu_body_str(u.body_type));
	if (u.file_form_name) printf("file_field:  %s\n", u.file_form_name);
	if (u.url_expr) printf("url_expr:    %s\n", u.url_expr);
	if (u.del_expr) printf("del_expr:    %s\n", u.del_expr);
	for (size_t i = 0; i < u.n_headers; i++) {
		printf("header:      %s: %s\n", u.headers[i].k, u.headers[i].v);
	}
	for (size_t i = 0; i < u.n_args; i++) {
		printf("arg:         %s = %s\n", u.args[i].k, u.args[i].v);
	}
	for (size_t i = 0; i < u.n_params; i++) {
		printf("param:       %s = %s\n", u.params[i].k, u.params[i].v);
	}
	sxcu_free(&u);
	return 0;
}

int cmd_sxcu(int argc, char **argv) {
	if (argc < 1) return usage();
	const char *sub = argv[0];
	if (strcmp(sub, "add") == 0) {
		if (argc != 2) return usage();
		if (sxcu_dir_add(argv[1]) != 0) return 1;
		log_info("sxcu: added to %s", sxcu_dir_path());
		return 0;
	}
	if (strcmp(sub, "list") == 0 || strcmp(sub, "ls") == 0) return do_list();
	if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0) {
		if (argc != 2) return usage();
		return sxcu_dir_remove(argv[1]) == 0 ? 0 : 1;
	}
	if (strcmp(sub, "show") == 0) {
		if (argc != 2) return usage();
		return do_show(argv[1]);
	}
	return usage();
}
