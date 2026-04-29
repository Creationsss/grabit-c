// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"
#include "paths.h"
#include "util.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "vendor/tomlc99/toml.h"

static const char *BOOL_KEYS[] = {
	"notifications",
	"sound",
	"save_captures",
	NULL,
};

static const char *KNOWN_TOP[] = {
	"default_action",
	"notifications",
	"sound",
	"save_captures",
	"save_dir",
	"editor",
	"filename",
	"filename_preset",
	"service",
	NULL,
};

static const char *KNOWN_SERVICES[] = {
	"zipline",
	"nest",
	"fakecrime",
	"ez",
	"guns",
	"pixelvault",
	NULL,
};

static const char *VALS_default_action[] = {"upload", "copy", "save", NULL};
static const char *VALS_filename_preset[] = {"date", "random", "uuid", "timestamp", NULL};

static const char *VALS_zl_format[] = {"random", "date", "uuid", "name", "gfycat", NULL};
static const char *VALS_zl_compress[] = {"jpg", "png", "webp", "jxl", NULL};
static const char *VALS_zl_true_only[] = {"true", NULL};

const struct zl_hdr gcfg_zl_headers[] = {
	{"x-zipline-deletes-at", ZL_FREE, NULL},
	{"x-zipline-format", ZL_ENUM, VALS_zl_format},
	{"x-zipline-image-compression-percent", ZL_INT_PCT, NULL},
	{"x-zipline-image-compression-type", ZL_ENUM, VALS_zl_compress},
	{"x-zipline-password", ZL_FREE, NULL},
	{"x-zipline-max-views", ZL_INT, NULL},
	{"x-zipline-no-json", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-original-name", ZL_ENUM, VALS_zl_true_only},
	{"x-zipline-folder", ZL_FREE, NULL},
	{"x-zipline-filename", ZL_FREE, NULL},
	{"x-zipline-domain", ZL_FREE, NULL},
	{"x-zipline-file-extension", ZL_FREE, NULL},
};
const size_t gcfg_zl_headers_n = sizeof gcfg_zl_headers / sizeof gcfg_zl_headers[0];

const struct zl_hdr *gcfg_zl_find(const char *name) {
	for (size_t i = 0; i < gcfg_zl_headers_n; i++) {
		if (strcmp(gcfg_zl_headers[i].name, name) == 0) return &gcfg_zl_headers[i];
	}
	return NULL;
}

static bool in_list(const char *needle, const char **list) {
	for (size_t i = 0; list[i]; i++) {
		if (strcmp(list[i], needle) == 0) return true;
	}
	return false;
}

static bool is_bool_key(const char *key) {
	return in_list(key, BOOL_KEYS);
}
static bool is_known_service(const char *s) {
	return in_list(s, KNOWN_SERVICES);
}

static int kv_grow(struct config *c, size_t need) {
	if (c->cap >= need) return 0;
	size_t cap = c->cap ? c->cap : 16;
	while (cap < need)
		cap *= 2;
	struct kv *p = realloc(c->kvs, cap * sizeof *p);
	if (!p) return -1;
	c->kvs = p;
	c->cap = cap;
	return 0;
}

static struct kv *kv_find(struct config *c, const char *key) {
	for (size_t i = 0; i < c->n; i++) {
		if (strcmp(c->kvs[i].key, key) == 0) return &c->kvs[i];
	}
	return NULL;
}

static int kv_upsert(struct config *c, const char *key, const char *val) {
	struct kv *e = kv_find(c, key);
	if (e) {
		char *nv = strdup(val);
		if (!nv) return -1;
		free(e->val);
		e->val = nv;
		return 0;
	}
	if (kv_grow(c, c->n + 1) != 0) return -1;
	c->kvs[c->n].key = strdup(key);
	c->kvs[c->n].val = strdup(val);
	if (!c->kvs[c->n].key || !c->kvs[c->n].val) {
		free(c->kvs[c->n].key);
		free(c->kvs[c->n].val);
		return -1;
	}
	c->n++;
	return 0;
}

static int flatten_table(toml_table_t *t, const char *prefix, struct config *c) {
	for (int i = 0;; i++) {
		const char *k = toml_key_in(t, i);
		if (!k) break;

		char *full = NULL;
		if (prefix && prefix[0]) {
			if (grabit_xasprintf(&full, "%s.%s", prefix, k) != 0) return -1;
		} else {
			full = strdup(k);
			if (!full) return -1;
		}

		toml_datum_t s = toml_string_in(t, k);
		if (s.ok) {
			int rc = kv_upsert(c, full, s.u.s);
			free(s.u.s);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t b = toml_bool_in(t, k);
		if (b.ok) {
			int rc = kv_upsert(c, full, b.u.b ? "true" : "false");
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_datum_t n = toml_int_in(t, k);
		if (n.ok) {
			char buf[32];
			snprintf(buf, sizeof buf, "%lld", (long long)n.u.i);
			int rc = kv_upsert(c, full, buf);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		toml_table_t *sub = toml_table_in(t, k);
		if (sub) {
			int rc = flatten_table(sub, full, c);
			free(full);
			if (rc != 0) return -1;
			continue;
		}

		log_warn("dropping unsupported config value at %s", full);
		free(full);
	}
	return 0;
}

static void seed_defaults(struct config *c) {
	kv_upsert(c, "default_action", "copy");
	kv_upsert(c, "notifications", "true");
	kv_upsert(c, "sound", "false");
	kv_upsert(c, "save_captures", "false");
}

int config_load(struct config *c) {
	memset(c, 0, sizeof *c);

	const char *file = paths_config_file();
	const char *dir = paths_config_dir();
	if (paths_mkdir_p(dir) != 0) {
		log_error("mkdir -p %s: %s", dir, strerror(errno));
		return -1;
	}

	struct stat st;
	bool first_run = stat(file, &st) != 0 || st.st_size == 0;
	if (first_run) {
		seed_defaults(c);
		if (config_save(c) != 0) {
			log_error("could not write default config to %s: %s", file, strerror(errno));
			config_free(c);
			return -1;
		}
		log_info("no config found at %s; wrote sensible defaults.", file);
		log_info("configure with: grabit set <key> <value>");
		return 0;
	}

	FILE *f = fopen(file, "r");
	if (!f) {
		log_error("open(%s): %s", file, strerror(errno));
		return -1;
	}
	char errbuf[256];
	toml_table_t *root = toml_parse_file(f, errbuf, sizeof errbuf);
	fclose(f);
	if (!root) {
		log_error("parse %s: %s", file, errbuf);
		return -1;
	}

	int rc = flatten_table(root, "", c);
	toml_free(root);
	if (rc != 0) {
		config_free(c);
		return -1;
	}
	return 0;
}

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
		if (is_bool_key(short_key) && (strcmp(val, "true") == 0 || strcmp(val, "false") == 0)) {
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
	return rc;

oom:
	grabit_buf_free(&out);
	errno = ENOMEM;
	return -1;
}

void config_free(struct config *c) {
	if (!c) return;
	for (size_t i = 0; i < c->n; i++) {
		free(c->kvs[i].key);
		free(c->kvs[i].val);
	}
	free(c->kvs);
	memset(c, 0, sizeof *c);
}

const char *config_get(struct config *c, const char *key) {
	struct kv *e = kv_find(c, key);
	return e ? e->val : NULL;
}

static bool valid_top_key(const char *key) {
	return in_list(key, KNOWN_TOP);
}

static bool valid_service_key(const char *key) {
	if (strncmp(key, "services.", 9) != 0) return false;
	const char *rest = key + 9;
	const char *dot = strchr(rest, '.');
	if (!dot) return false;
	char svc[32];
	size_t svc_len = (size_t)(dot - rest);
	if (svc_len == 0 || svc_len >= sizeof svc) return false;
	memcpy(svc, rest, svc_len);
	svc[svc_len] = '\0';
	if (!is_known_service(svc)) return false;

	const char *leaf = dot + 1;
	if (strcmp(leaf, "auth") == 0) return true;
	if (strcmp(leaf, "domain") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "folder") == 0) return strcmp(svc, "nest") == 0;
	if (strncmp(leaf, "headers.", 8) == 0) return strcmp(svc, "zipline") == 0 && leaf[8] != '\0';
	return false;
}

static bool valid_recording_key(const char *key) {
	if (strncmp(key, "recording.", 10) != 0) return false;
	const char *leaf = key + 10;
	if (strcmp(leaf, "fps") == 0) return true;
	if (strcmp(leaf, "crf") == 0) return true;
	if (strcmp(leaf, "max_size_mb") == 0) return true;
	if (strcmp(leaf, "cursor") == 0) return true;
	if (strcmp(leaf, "ffmpeg") == 0) return true;
	if (strcmp(leaf, "preset") == 0) return true;
	if (strcmp(leaf, "tune") == 0) return true;
	if (strcmp(leaf, "pix_fmt") == 0) return true;
	return false;
}

static const char *VALS_x264_tune[] = {
	"film",
	"animation",
	"grain",
	"stillimage",
	"psnr",
	"ssim",
	"fastdecode",
	"zerolatency",
	NULL,
};

static const char *VALS_pix_fmt[] = {
	"yuv420p",
	"yuv422p",
	"yuv444p",
	"yuv420p10le",
	NULL,
};

static const char *VALS_x264_preset[] = {
	"ultrafast",
	"superfast",
	"veryfast",
	"faster",
	"fast",
	"medium",
	"slow",
	"slower",
	"veryslow",
	NULL,
};

static int validate_int_in_range(const char *key, const char *value, long lo, long hi) {
	if (!*value) {
		log_error("%s must be an integer", key);
		return -1;
	}
	char *end = NULL;
	long n = strtol(value, &end, 10);
	if (!end || *end != '\0') {
		log_error("%s must be an integer", key);
		return -1;
	}
	if (n < lo || n > hi) {
		log_error("%s must be between %ld and %ld", key, lo, hi);
		return -1;
	}
	return 0;
}

int config_set(struct config *c, const char *key, const char *value) {
	if (!valid_top_key(key) && !valid_service_key(key) && !valid_recording_key(key)) {
		log_error("unknown config key: %s", key);
		return -1;
	}
	if (strcmp(key, "recording.fps") == 0 &&
		validate_int_in_range(key, value, 1, 120) != 0) return -1;
	if (strcmp(key, "recording.crf") == 0 &&
		validate_int_in_range(key, value, 0, 51) != 0) return -1;
	if (strcmp(key, "recording.max_size_mb") == 0 &&
		validate_int_in_range(key, value, 0, 100000) != 0) return -1;
	if (strcmp(key, "recording.cursor") == 0 &&
		strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
		log_error("recording.cursor must be true or false");
		return -1;
	}
	if (strcmp(key, "recording.preset") == 0 && !in_list(value, VALS_x264_preset)) {
		log_error("recording.preset must be one of "
				  "ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow");
		return -1;
	}
	if (strcmp(key, "recording.tune") == 0 && value[0] && !in_list(value, VALS_x264_tune)) {
		log_error("recording.tune must be one of "
				  "film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency");
		return -1;
	}
	if (strcmp(key, "recording.pix_fmt") == 0 && !in_list(value, VALS_pix_fmt)) {
		log_error("recording.pix_fmt must be one of yuv420p|yuv422p|yuv444p|yuv420p10le");
		return -1;
	}
	if (strcmp(key, "default_action") == 0 && !in_list(value, VALS_default_action)) {
		log_error("default_action must be one of upload|copy|save");
		return -1;
	}
	if (strcmp(key, "filename_preset") == 0 && !in_list(value, VALS_filename_preset)) {
		log_error("filename_preset must be one of date|random|uuid|timestamp");
		return -1;
	}
	if (strcmp(key, "service") == 0 && !is_known_service(value)) {
		log_error("service must be one of zipline|nest|fakecrime|ez|guns|pixelvault");
		return -1;
	}
	if (is_bool_key(key) && strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
		log_error("%s must be true or false", key);
		return -1;
	}

	const char *zl_prefix = "services.zipline.headers.";
	if (strncmp(key, zl_prefix, strlen(zl_prefix)) == 0) {
		const char *hdr = key + strlen(zl_prefix);
		const struct zl_hdr *spec = gcfg_zl_find(hdr);
		if (!spec) {
			log_warn("unknown zipline header %s; forwarding as-is", hdr);
		} else {
			switch (spec->kind) {
			case ZL_FREE:
				break;
			case ZL_ENUM:
				if (!in_list(value, spec->allowed)) {
					if (spec->allowed[0] && !spec->allowed[1]) {
						log_error("%s must be \"%s\" (omit the header to disable)",
								  hdr, spec->allowed[0]);
					} else {
						struct grabit_buf b = {0};
						for (size_t i = 0; spec->allowed[i]; i++) {
							if (i) grabit_buf_putc(&b, '|');
							grabit_buf_puts(&b, spec->allowed[i]);
						}
						log_error("%s must be one of %s", hdr, b.data ? b.data : "(none)");
						grabit_buf_free(&b);
					}
					return -1;
				}
				break;
			case ZL_INT:
			case ZL_INT_PCT: {
				if (!*value) {
					log_error("%s must be an integer", hdr);
					return -1;
				}
				char *end = NULL;
				long n = strtol(value, &end, 10);
				if (!end || *end != '\0') {
					log_error("%s must be an integer", hdr);
					return -1;
				}
				if (spec->kind == ZL_INT_PCT && (n < 0 || n > 100)) {
					log_error("%s must be between 0 and 100", hdr);
					return -1;
				}
				if (spec->kind == ZL_INT && n < 0) {
					log_error("%s must be a non-negative integer", hdr);
					return -1;
				}
				break;
			}
			}
		}
	}

	char *normalized = NULL;
	if (strcmp(key, "services.zipline.domain") == 0 && value && *value) {
		bool has_scheme = strncmp(value, "http://", 7) == 0 ||
						  strncmp(value, "https://", 8) == 0;
		size_t vlen = strlen(value);
		while (vlen > 0 && value[vlen - 1] == '/')
			vlen--;
		const char *suffix = "/api/upload";
		size_t slen = strlen(suffix);
		bool has_path = vlen >= slen && strncmp(value + vlen - slen, suffix, slen) == 0;
		int rc;
		if (has_scheme && has_path)
			rc = grabit_xasprintf(&normalized, "%.*s", (int)vlen, value);
		else if (has_scheme)
			rc = grabit_xasprintf(&normalized, "%.*s/api/upload", (int)vlen, value);
		else if (has_path)
			rc = grabit_xasprintf(&normalized, "https://%.*s", (int)vlen, value);
		else
			rc = grabit_xasprintf(&normalized, "https://%.*s/api/upload", (int)vlen, value);
		if (rc != 0) {
			log_error("oom: config_set");
			return -1;
		}
		value = normalized;
	}

	int rc = kv_upsert(c, key, value);
	free(normalized);
	if (rc != 0) {
		log_error("oom: config_set");
		return -1;
	}
	return config_save(c);
}

bool config_needs_setup(struct config *c) {
	return config_get(c, "default_action") == NULL;
}
