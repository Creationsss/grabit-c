// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "util/json_path.h"

#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <json-c/json.h>

static struct json_object *walk_path(struct json_object *root, const char *path) {
	struct json_object *cur = root;
	const char *p = path;
	char key[256];

	while (*p && cur) {
		if (*p == '.') p++;

		if (*p == '[') {
			p++;
			unsigned long idx = 0;
			if (!isdigit((unsigned char)*p)) return NULL;
			while (isdigit((unsigned char)*p)) {
				if (idx > (UINT32_MAX - 9) / 10) return NULL;
				idx = idx * 10 + (unsigned long)(*p - '0');
				p++;
			}
			if (*p != ']') return NULL;
			p++;
			if (json_object_get_type(cur) != json_type_array) return NULL;
			cur = json_object_array_get_idx(cur, (size_t)idx);
			continue;
		}

		size_t k = 0;
		while (*p && *p != '.' && *p != '[' && k + 1 < sizeof key) {
			key[k++] = *p++;
		}
		key[k] = '\0';
		if (k == 0) return NULL;

		if (json_object_get_type(cur) != json_type_object) return NULL;
		struct json_object *next;
		if (!json_object_object_get_ex(cur, key, &next)) return NULL;
		cur = next;
	}
	return cur;
}

char *grabit_json_path_string(struct json_object *root, const char *path) {
	struct json_object *v = walk_path(root, path);
	if (!v) return NULL;
	if (json_object_get_type(v) != json_type_string) return NULL;
	const char *s = json_object_get_string(v);
	if (!s || !s[0]) return NULL;
	return strdup(s);
}
