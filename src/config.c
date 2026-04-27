// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "log.h"
#include "paths.h"

#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <json-c/json.h>

struct schema_entry {
	const char *key;
	const char *regex;
	bool        required;
};

#define RX_LINE "^[^[:cntrl:]]+$"
#define RX_BOOL "^(true|false)$"
#define RX_NUM  "^[0-9]+$"
#define RX_UUID "^[a-fA-F0-9]{8}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{4}-[a-fA-F0-9]{12}$"
#define RX_SERVICE "^(zipline|nest|fakecrime|ez|guns|pixelvault)$"

static const struct schema_entry SCHEMA[] = {
	{ "DEFAULT_OPTION",                       "^(upload|save|copy)$",            true  },
	{ "SHOW_NOTIFICATIONS",                   RX_BOOL,                           true  },
	{ "SNIP_SOUND",                           RX_BOOL,                           true  },
	{ "SAVE_IMAGES",                          RX_BOOL,                           true  },
	{ "SAVE_DIR",                             RX_LINE,                           false },
	{ "DOMAIN",                               RX_LINE,                           false },
	{ "SERVICE",                              RX_SERVICE,                        false },
	{ "IMAGE_EDITOR",                         RX_LINE,                           false },
	{ "FILE_NAME_FORMAT",                     "^(date|random|uuid|timestamp)$",  false },
	{ "FILENAME_TEMPLATE",                    RX_LINE,                           false },

	{ "nest_folder",                          RX_UUID,                           false },

	{ "zipline_auth",                         RX_LINE,                           false },
	{ "nest_auth",                            RX_LINE,                           false },
	{ "fakecrime_auth",                       RX_LINE,                           false },
	{ "ez_auth",                              RX_LINE,                           false },
	{ "guns_auth",                            RX_LINE,                           false },
	{ "pixelvault_auth",                      RX_LINE,                           false },

	{ "x-zipline-max-views",                  RX_NUM,                            false },
	{ "x-zipline-image-compression-percent",  RX_NUM,                            false },
	{ "x-zipline-original-name",              RX_BOOL,                           false },
	{ "x-zipline-format",                     "^(date|random|uuid|name|gfycat)$", false },
	{ "x-zipline-domain",                     RX_LINE,                           false },
};

static const size_t SCHEMA_LEN = sizeof SCHEMA / sizeof SCHEMA[0];

static const struct schema_entry *schema_find(const char *key) {
	for (size_t i = 0; i < SCHEMA_LEN; i++) {
		if (strcmp(SCHEMA[i].key, key) == 0) return &SCHEMA[i];
	}
	return NULL;
}

static bool schema_value_ok(const struct schema_entry *e, const char *value) {
	regex_t rx;
	int rc = regcomp(&rx, e->regex, REG_EXTENDED | REG_NOSUB);
	if (rc != 0) {
		char buf[256];
		regerror(rc, &rx, buf, sizeof buf);
		die("internal: regcomp(%s): %s", e->regex, buf);
	}
	bool ok = regexec(&rx, value, 0, NULL, 0) == 0;
	regfree(&rx);
	return ok;
}

static struct json_object *defaults_object(void) {
	struct json_object *o = json_object_new_object();
	json_object_object_add(o, "DEFAULT_OPTION",     json_object_new_string("copy"));
	json_object_object_add(o, "SHOW_NOTIFICATIONS", json_object_new_string("true"));
	json_object_object_add(o, "SNIP_SOUND",         json_object_new_string("true"));
	json_object_object_add(o, "SAVE_IMAGES",        json_object_new_string("false"));
	return o;
}

static void log_first_run_hint(void) {
	log_info("no config found at %s", paths_config_file());
	log_info("wrote sensible defaults; configure with:");
	log_info("  grabit set DEFAULT_OPTION upload");
	log_info("  grabit set SERVICE zipline");
	log_info("  grabit set zipline_auth <token>");
	log_info("  grabit set DOMAIN https://<your-domain>/api/upload");
}

static int validate_in_place(struct config *c) {
	char **to_drop = NULL;
	size_t n_drop = 0, cap_drop = 0;

	struct json_object_iterator it = json_object_iter_begin(c->root);
	struct json_object_iterator end = json_object_iter_end(c->root);

	while (!json_object_iter_equal(&it, &end)) {
		const char *k = json_object_iter_peek_name(&it);
		struct json_object *v = json_object_iter_peek_value(&it);
		bool drop = false;

		const struct schema_entry *e = schema_find(k);
		if (!e) {
			log_warn("dropping unknown config key: %s", k);
			drop = true;
		} else if (json_object_get_type(v) != json_type_string) {
			log_warn("dropping %s: expected string, got %s", k,
			         json_type_to_name(json_object_get_type(v)));
			drop = true;
		} else if (!schema_value_ok(e, json_object_get_string(v))) {
			log_warn("dropping %s: value %s does not match schema", k,
			         json_object_get_string(v));
			drop = true;
		}

		if (drop) {
			if (n_drop == cap_drop) {
				size_t ncap = cap_drop ? cap_drop * 2 : 8;
				char **p = realloc(to_drop, ncap * sizeof *p);
				if (p) {
					to_drop = p;
					cap_drop = ncap;
				}
			}
			if (n_drop < cap_drop) {
				char *copy = strdup(k);
				if (copy) to_drop[n_drop++] = copy;
			}
		}
		json_object_iter_next(&it);
	}

	for (size_t i = 0; i < n_drop; i++) {
		json_object_object_del(c->root, to_drop[i]);
		free(to_drop[i]);
	}
	free(to_drop);
	return (int)n_drop;
}

int config_load(struct config *c) {
	c->root = NULL;

	const char *file = paths_config_file();
	const char *dir  = paths_config_dir();

	if (paths_mkdir_p(dir) != 0) {
		log_error("mkdir -p %s: %s", dir, strerror(errno));
		return -1;
	}

	struct stat st;
	bool first_run = stat(file, &st) != 0 || st.st_size == 0;

	if (first_run) {
		c->root = defaults_object();
		if (!c->root) {
			log_error("oom: defaults_object");
			return -1;
		}
		if (config_save(c) != 0) {
			log_error("could not write default config to %s: %s", file, strerror(errno));
			json_object_put(c->root);
			c->root = NULL;
			return -1;
		}
		log_first_run_hint();
		return 0;
	}

	c->root = json_object_from_file(file);
	if (!c->root) {
		log_error("could not parse %s as JSON", file);
		return -1;
	}
	if (json_object_get_type(c->root) != json_type_object) {
		log_warn("%s: top-level is not a JSON object — replacing with defaults", file);
		json_object_put(c->root);
		c->root = defaults_object();
		(void)config_save(c);
		return 0;
	}

	int dropped = validate_in_place(c);
	if (dropped > 0) {
		(void)config_save(c);
	}
	return 0;
}

int config_save(struct config *c) {
	if (!c->root) {
		errno = EINVAL;
		return -1;
	}
	const char *json = json_object_to_json_string_ext(c->root, JSON_C_TO_STRING_PRETTY);
	if (!json) {
		errno = EIO;
		return -1;
	}
	return paths_atomic_write(paths_config_file(), json, strlen(json));
}

void config_free(struct config *c) {
	if (!c) return;
	if (c->root) json_object_put(c->root);
	c->root = NULL;
}

const char *config_get(struct config *c, const char *key) {
	if (!c->root) return NULL;
	struct json_object *v;
	if (!json_object_object_get_ex(c->root, key, &v)) return NULL;
	if (json_object_get_type(v) != json_type_string) return NULL;
	return json_object_get_string(v);
}

int config_set(struct config *c, const char *key, const char *value) {
	const struct schema_entry *e = schema_find(key);
	if (!e) {
		log_error("unknown config key: %s", key);
		return -1;
	}
	if (!schema_value_ok(e, value)) {
		log_error("invalid value for %s: %s (must match %s)", key, value, e->regex);
		return -1;
	}
	struct json_object *jv = json_object_new_string(value);
	if (!jv) {
		log_error("oom: config_set");
		return -1;
	}
	if (json_object_object_add(c->root, key, jv) != 0) {
		json_object_put(jv);
		log_error("config_set: json_object_object_add failed");
		return -1;
	}
	return config_save(c);
}

bool config_needs_setup(struct config *c) {
	if (!c->root) return true;
	for (size_t i = 0; i < SCHEMA_LEN; i++) {
		if (!SCHEMA[i].required) continue;
		struct json_object *v;
		if (!json_object_object_get_ex(c->root, SCHEMA[i].key, &v)) return true;
	}
	return false;
}

int cmd_set(int argc, char **argv) {
	if (argc != 2) {
		log_error("usage: grabit set <key> <value>");
		return 1;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;
	int rc = config_set(&c, argv[0], argv[1]);
	config_free(&c);
	if (rc != 0) return 1;
	log_info("set %s = %s", argv[0], argv[1]);
	return 0;
}

int cmd_get(int argc, char **argv) {
	if (argc > 1) {
		log_error("usage: grabit get [<key>]");
		return 1;
	}
	struct config c;
	if (config_load(&c) != 0) return 1;

	int rc = 0;
	if (argc == 0) {
		const char *s = json_object_to_json_string_ext(c.root, JSON_C_TO_STRING_PRETTY);
		if (s) {
			fputs(s, stdout);
			fputc('\n', stdout);
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
