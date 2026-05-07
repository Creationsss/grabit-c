// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"
#include "util.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *BOOL_KEYS[] = {
	"notifications",
	"save_captures",
	NULL,
};

static const char *KNOWN_TOP[] = {
	"default_action",
	"notifications",
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

static const char *VALS_default_action[] = {"upload", "copy", "save", "pin", NULL};
static const char *VALS_filename_preset[] = {"date", "random", "uuid", "timestamp", NULL};
static const char *VALS_edit_color[] = {"red", "yellow", "green", "blue", "black", "white", NULL};

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

bool cfg_in_list(const char *needle, const char **list) {
	for (size_t i = 0; list[i]; i++) {
		if (strcmp(list[i], needle) == 0) return true;
	}
	return false;
}

bool cfg_is_bool_key(const char *key) {
	return cfg_in_list(key, BOOL_KEYS);
}

bool cfg_is_known_service(const char *s) {
	return cfg_in_list(s, KNOWN_SERVICES);
}

static bool valid_top_key(const char *key) {
	return cfg_in_list(key, KNOWN_TOP);
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
	if (!cfg_is_known_service(svc)) return false;

	const char *leaf = dot + 1;
	if (strcmp(leaf, "auth") == 0) return true;
	if (strcmp(leaf, "domain") == 0) return strcmp(svc, "zipline") == 0;
	if (strcmp(leaf, "folder") == 0) return strcmp(svc, "nest") == 0;
	if (strncmp(leaf, "headers.", 8) == 0) return strcmp(svc, "zipline") == 0 && leaf[8] != '\0';
	return false;
}

static bool valid_ocr_key(const char *key) {
	if (strncmp(key, "ocr.", 4) != 0) return false;
	return strcmp(key + 4, "tesseract") == 0;
}

static bool valid_edit_key(const char *key) {
	if (strncmp(key, "edit.", 5) != 0) return false;
	const char *leaf = key + 5;
	return strcmp(leaf, "color") == 0 || strcmp(leaf, "width") == 0;
}

static bool valid_sound_key(const char *key) {
	if (strncmp(key, "sound.", 6) != 0) return false;
	const char *leaf = key + 6;
	return strcmp(leaf, "enabled") == 0 || strcmp(leaf, "player") == 0 ||
		   strcmp(leaf, "file") == 0;
}

static bool valid_recording_key(const char *key) {
	if (strncmp(key, "recording.", 10) != 0) return false;
	const char *leaf = key + 10;
	return strcmp(leaf, "fps") == 0 || strcmp(leaf, "crf") == 0 ||
		   strcmp(leaf, "max_size_mb") == 0 || strcmp(leaf, "cursor") == 0 ||
		   strcmp(leaf, "ffmpeg") == 0 || strcmp(leaf, "preset") == 0 ||
		   strcmp(leaf, "tune") == 0 || strcmp(leaf, "pix_fmt") == 0;
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

static int validate_zl_header(const char *hdr, const char *value) {
	const struct zl_hdr *spec = gcfg_zl_find(hdr);
	if (!spec) {
		log_warn("unknown zipline header %s; forwarding as-is", hdr);
		return 0;
	}
	switch (spec->kind) {
	case ZL_FREE:
		return 0;
	case ZL_ENUM:
		if (cfg_in_list(value, spec->allowed)) return 0;
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
		return 0;
	}
	}
	return -1;
}

static char *normalize_zipline_domain(const char *value) {
	if (!value || !*value) return NULL;
	bool has_scheme = strncmp(value, "http://", 7) == 0 ||
					  strncmp(value, "https://", 8) == 0;
	size_t vlen = strlen(value);
	while (vlen > 0 && value[vlen - 1] == '/')
		vlen--;
	const char *suffix = "/api/upload";
	size_t slen = strlen(suffix);
	bool has_path = vlen >= slen && strncmp(value + vlen - slen, suffix, slen) == 0;
	char *out = NULL;
	int rc;
	if (has_scheme && has_path)
		rc = grabit_xasprintf(&out, "%.*s", (int)vlen, value);
	else if (has_scheme)
		rc = grabit_xasprintf(&out, "%.*s/api/upload", (int)vlen, value);
	else if (has_path)
		rc = grabit_xasprintf(&out, "https://%.*s", (int)vlen, value);
	else
		rc = grabit_xasprintf(&out, "https://%.*s/api/upload", (int)vlen, value);
	return rc == 0 ? out : NULL;
}

static int validate_edit_color(const char *value) {
	const char *p = (*value == '#') ? value + 1 : value;
	size_t len = strlen(p);
	bool valid_hex = (len == 6 || len == 3);
	if (valid_hex) {
		for (size_t i = 0; i < len; i++) {
			char hc = p[i];
			if (!((hc >= '0' && hc <= '9') || (hc >= 'a' && hc <= 'f') ||
				  (hc >= 'A' && hc <= 'F'))) {
				valid_hex = false;
				break;
			}
		}
	}
	if (!valid_hex && !cfg_in_list(value, VALS_edit_color)) {
		log_error("edit.color must be #RRGGBB or one of red|yellow|green|blue|black|white");
		return -1;
	}
	return 0;
}

int config_set(struct config *c, const char *key, const char *value) {
	if (!valid_top_key(key) && !valid_service_key(key) && !valid_recording_key(key) &&
		!valid_ocr_key(key) && !valid_sound_key(key) && !valid_edit_key(key)) {
		log_error("unknown config key: %s", key);
		return -1;
	}
	if (strcmp(key, "sound.enabled") == 0 &&
		strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
		log_error("sound.enabled must be true or false");
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
	if (strcmp(key, "recording.preset") == 0 && !cfg_in_list(value, VALS_x264_preset)) {
		log_error("recording.preset must be one of "
				  "ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow");
		return -1;
	}
	if (strcmp(key, "recording.tune") == 0 && value[0] && !cfg_in_list(value, VALS_x264_tune)) {
		log_error("recording.tune must be one of "
				  "film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency");
		return -1;
	}
	if (strcmp(key, "recording.pix_fmt") == 0 && !cfg_in_list(value, VALS_pix_fmt)) {
		log_error("recording.pix_fmt must be one of yuv420p|yuv422p|yuv444p|yuv420p10le");
		return -1;
	}
	if (strcmp(key, "default_action") == 0 && !cfg_in_list(value, VALS_default_action)) {
		log_error("default_action must be one of upload|copy|save|pin");
		return -1;
	}
	if (strcmp(key, "filename_preset") == 0 && !cfg_in_list(value, VALS_filename_preset)) {
		log_error("filename_preset must be one of date|random|uuid|timestamp");
		return -1;
	}
	if (strcmp(key, "service") == 0 && !cfg_is_known_service(value)) {
		log_error("service must be one of zipline|nest|fakecrime|ez|guns|pixelvault");
		return -1;
	}
	if (strcmp(key, "edit.color") == 0 && validate_edit_color(value) != 0) return -1;
	if (strcmp(key, "edit.width") == 0) {
		char *end = NULL;
		long v = strtol(value, &end, 10);
		if (!*value || end == value || *end || v < 1 || v > 20) {
			log_error("edit.width must be an integer between 1 and 20");
			return -1;
		}
	}
	if (cfg_is_bool_key(key) && strcmp(value, "true") != 0 && strcmp(value, "false") != 0) {
		log_error("%s must be true or false", key);
		return -1;
	}

	const char *zl_prefix = "services.zipline.headers.";
	if (strncmp(key, zl_prefix, strlen(zl_prefix)) == 0) {
		if (validate_zl_header(key + strlen(zl_prefix), value) != 0) return -1;
	}

	char *normalized = NULL;
	if (strcmp(key, "services.zipline.domain") == 0) {
		normalized = normalize_zipline_domain(value);
		if (!normalized) {
			log_error("oom: config_set");
			return -1;
		}
		value = normalized;
	}

	int rc = cfg_kv_upsert(c, key, value);
	free(normalized);
	if (rc != 0) {
		log_error("oom: config_set");
		return -1;
	}
	return config_save(c);
}
