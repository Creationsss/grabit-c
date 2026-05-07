// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "paths.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gcfg_cmp_kv(const void *a, const void *b) {
	const struct kv *ka = a;
	const struct kv *kb = b;
	bool ad = strchr(ka->key, '.') != NULL;
	bool bd = strchr(kb->key, '.') != NULL;
	if (ad != bd) return ad ? 1 : -1;
	return strcmp(ka->key, kb->key);
}

static int section_depth(const char *key) {
	int d = 0;
	for (const char *p = key; *p; p++)
		if (*p == '.') d++;
	return d;
}

static void emit_string_value(struct grabit_buf *out, const char *s) {
	grabit_buf_putc(out, '"');
	for (const char *p = s; *p; p++) {
		unsigned char ch = (unsigned char)*p;
		if (ch == '"' || ch == '\\') {
			grabit_buf_putc(out, '\\');
			grabit_buf_putc(out, (char)ch);
		} else if (ch < 0x20) {
			char esc[8];
			snprintf(esc, sizeof esc, "\\u%04x", ch);
			grabit_buf_puts(out, esc);
		} else {
			grabit_buf_putc(out, (char)ch);
		}
	}
	grabit_buf_putc(out, '"');
}

static bool key_needs_quoting(const char *k) {
	if (!*k) return true;
	for (const char *p = k; *p; p++) {
		unsigned char ch = (unsigned char)*p;
		if (!(isalnum(ch) || ch == '_' || ch == '-')) return true;
	}
	return false;
}

static void emit_bare_or_quoted_key(struct grabit_buf *out, const char *k) {
	if (key_needs_quoting(k)) {
		emit_string_value(out, k);
	} else {
		grabit_buf_puts(out, k);
	}
}

static int kv_strcmp_cmp(const void *a, const void *b) {
	const struct kv *ka = a;
	const struct kv *kb = b;
	return strcmp(ka->key, kb->key);
}

int config_save(struct config *c) {
	if (c->n > 1) qsort(c->kvs, c->n, sizeof *c->kvs, gcfg_cmp_kv);

	struct grabit_buf out = {0};
	const char *current_section = NULL;
	size_t current_section_len = 0;

	for (size_t i = 0; i < c->n; i++) {
		const char *key = c->kvs[i].key;
		bool is_top = section_depth(key) == 0;

		if (is_top) {
			if (current_section) {
				current_section = NULL;
				current_section_len = 0;
			}
		} else {
			const char *last_dot = strrchr(key, '.');
			size_t prefix_len = (size_t)(last_dot - key);
			if (!current_section ||
				current_section_len != prefix_len ||
				strncmp(current_section, key, prefix_len) != 0) {
				if (out.len > 0) grabit_buf_putc(&out, '\n');
				grabit_buf_putc(&out, '[');
				char *prefix = strndup(key, prefix_len);
				if (!prefix) goto oom;
				const char *seg = prefix;
				bool first_seg = true;
				char *dot;
				while ((dot = strchr(seg, '.')) != NULL) {
					if (!first_seg) grabit_buf_putc(&out, '.');
					*dot = '\0';
					emit_bare_or_quoted_key(&out, seg);
					seg = dot + 1;
					first_seg = false;
				}
				if (!first_seg) grabit_buf_putc(&out, '.');
				emit_bare_or_quoted_key(&out, seg);
				free(prefix);
				grabit_buf_puts(&out, "]\n");
				current_section = key;
				current_section_len = prefix_len;
			}
		}

		const char *short_key = is_top ? key : strrchr(key, '.') + 1;
		emit_bare_or_quoted_key(&out, short_key);
		grabit_buf_puts(&out, " = ");
		const char *val = c->kvs[i].val;
		if (cfg_is_bool_key(short_key) && (strcmp(val, "true") == 0 || strcmp(val, "false") == 0)) {
			grabit_buf_puts(&out, val);
		} else {
			emit_string_value(&out, val);
		}
		grabit_buf_putc(&out, '\n');
	}

	if (out.len == 0) grabit_buf_putc(&out, '\n');
	int rc = paths_atomic_write(paths_config_file(), out.data, out.len);
	grabit_buf_free(&out);
	(void)current_section_len;
	if (c->n > 1) qsort(c->kvs, c->n, sizeof *c->kvs, kv_strcmp_cmp);
	return rc;

oom:
	grabit_buf_free(&out);
	if (c->n > 1) qsort(c->kvs, c->n, sizeof *c->kvs, kv_strcmp_cmp);
	errno = ENOMEM;
	return -1;
}
