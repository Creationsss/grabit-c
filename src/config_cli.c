// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool is_help_arg(const char *s) {
	return s && (strcmp(s, "--help") == 0 || strcmp(s, "-h") == 0);
}

static char *split_eq(const char *arg, const char **val_out) {
	const char *eq = strchr(arg, '=');
	if (!eq) return NULL;
	*val_out = eq + 1;
	char *k = strndup(arg, (size_t)(eq - arg));
	return k;
}

int cmd_set(int argc, char **argv) {
	if (argc == 0 || (argc == 1 && is_help_arg(argv[0]))) {
		cfg_help_print_all_keys();
		return argc == 0 ? 0 : 0;
	}

	const char *eq_val = NULL;
	char *eq_key = (argc == 1) ? split_eq(argv[0], &eq_val) : NULL;
	if (eq_key) {
		char *aliased[2] = {eq_key, (char *)eq_val};
		int rc = cmd_set(2, aliased);
		free(eq_key);
		return rc;
	}

	if (argc == 1) {
		const char *ex = NULL, *def = NULL;
		if (cfg_help_example_for_key(argv[0], &ex, &def) != 0) {
			log_error("unknown key: %s", argv[0]);
			log_info("run `grabit set` to see all keys");
			return 1;
		}
		printf("%s = ", argv[0]);
		bool starred = cfg_help_print_example(ex, def);
		printf("\n");
		if (def) {
			if (starred)
				printf("(* = default)\n");
			else
				printf("default: %s\n", def);
		}

		struct config c = {0};
		const char *current = NULL;
		bool loaded = config_load(&c) == 0;
		if (loaded) current = config_get(&c, argv[0]);
		printf("current: %s\n", current ? current : "(unset)");
		if (loaded) config_free(&c);
		return 0;
	}
	if (argc != 2) {
		log_error("usage: grabit set <key> <value>");
		return 1;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;
	int rc = config_set(&c, argv[0], argv[1]);
	const char *stored = (rc == 0) ? config_get(&c, argv[0]) : NULL;
	if (rc == 0) log_info("set %s = %s", argv[0], stored ? stored : argv[1]);
	config_free(&c);
	return rc == 0 ? 0 : 1;
}

int cmd_get(int argc, char **argv) {
	if (argc == 1 && is_help_arg(argv[0])) {
		puts("usage: grabit get [<key>]");
		return 0;
	}
	if (argc > 1) {
		log_error("usage: grabit get [<key>]");
		return 1;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	int rc = 0;
	if (argc == 0) {
		if (c.n > 1) qsort(c.kvs, c.n, sizeof *c.kvs, gcfg_cmp_kv);
		for (size_t i = 0; i < c.n; i++) {
			printf("%s = %s\n", c.kvs[i].key, c.kvs[i].val);
		}
	} else {
		const char *v = config_get(&c, argv[0]);
		if (v) {
			puts(v);
		} else {
			log_error("not set: %s", argv[0]);
			rc = 1;
		}
	}
	config_free(&c);
	return rc;
}

int cmd_unset(int argc, char **argv) {
	if (argc == 1 && is_help_arg(argv[0])) {
		puts("usage: grabit unset <key>");
		return 0;
	}
	if (argc != 1) {
		log_error("usage: grabit unset <key>");
		return 1;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	int rc = 0;
	bool found = false;
	for (size_t i = 0; i < c.n; i++) {
		if (strcmp(c.kvs[i].key, argv[0]) != 0) continue;
		free(c.kvs[i].key);
		free(c.kvs[i].val);
		if (i + 1 < c.n) {
			memmove(&c.kvs[i], &c.kvs[i + 1], (c.n - i - 1) * sizeof *c.kvs);
		}
		c.n--;
		found = true;
		break;
	}
	if (!found) {
		log_info("%s was not set", argv[0]);
	} else if (config_save(&c) != 0) {
		log_error("could not save config");
		rc = 1;
	} else {
		log_info("unset %s", argv[0]);
	}
	config_free(&c);
	return rc;
}
