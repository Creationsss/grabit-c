// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

struct example {
	const char *key;
	const char *example;
	const char *def;
};

static const struct example TOP_EXAMPLES[] = {
	{"default_action", "upload|copy|save|pin", "copy"},
	{"notifications", "true|false", "true"},
	{"save_captures", "true|false", "false"},
	{"save_dir", "~/Pictures", NULL},
	{"editor", "satty | swappy | gimp | krita | kolourpaint", NULL},
	{"filename", "%Y-%m-%d-%H-%M-%S", NULL},
	{"filename_preset", "date|random|uuid|timestamp", "date"},
	{"service", "zipline|nest|fakecrime|ez|guns|pixelvault", NULL},
	{"format", "png|jpeg|webp", "png"},
};
static const size_t TOP_EXAMPLES_N = sizeof TOP_EXAMPLES / sizeof TOP_EXAMPLES[0];

static const char *zl_header_example(const struct zl_hdr *h) {
	static char buf[160];
	switch (h->kind) {
	case ZL_FREE:
		if (strcmp(h->name, "x-zipline-deletes-at") == 0) return "1d";
		if (strcmp(h->name, "x-zipline-domain") == 0) return "cdn1.example.com,cdn2.example.com";
		if (strcmp(h->name, "x-zipline-file-extension") == 0) return ".png";
		if (strcmp(h->name, "x-zipline-folder") == 0) return "<folder-id>";
		if (strcmp(h->name, "x-zipline-filename") == 0) return "<override>";
		return "<string>";
	case ZL_ENUM: {
		size_t off = 0;
		buf[0] = '\0';
		for (size_t i = 0; h->allowed[i]; i++) {
			int n = snprintf(buf + off, sizeof buf - off, "%s%s", i ? "|" : "", h->allowed[i]);
			if (n < 0 || (size_t)n >= sizeof buf - off) break;
			off += (size_t)n;
		}
		return buf;
	}
	case ZL_INT:
		return "<integer>";
	case ZL_INT_PCT:
		return "0-100";
	}
	return "";
}

static const char *const ALL_KNOWN_KEYS[] = {
	"default_action", "notifications", "save_captures", "save_dir", "editor",
	"filename", "filename_preset", "service", "format",
	"recording.fps", "recording.crf", "recording.preset", "recording.tune",
	"recording.pix_fmt", "recording.max_size_mb", "recording.cursor", "recording.ffmpeg",
	"sound.enabled", "sound.player", "sound.file",
	"edit.color", "edit.width",
	"jpeg.quality", "webp.quality", "webp.lossless",
	"ocr.tesseract",
	"services.zipline.auth", "services.zipline.domain",
	"services.nest.auth", "services.nest.folder",
	"services.fakecrime.auth", "services.ez.auth",
	"services.guns.auth", "services.pixelvault.auth",
	NULL,
};

static size_t edit_distance(const char *a, const char *b) {
	size_t la = strlen(a), lb = strlen(b);
	if (la > 64 || lb > 64) return 999;
	size_t prev[66], curr[66];
	for (size_t j = 0; j <= lb; j++) prev[j] = j;
	for (size_t i = 1; i <= la; i++) {
		curr[0] = i;
		for (size_t j = 1; j <= lb; j++) {
			size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
			size_t del = prev[j] + 1;
			size_t ins = curr[j - 1] + 1;
			size_t sub = prev[j - 1] + cost;
			size_t m = del < ins ? del : ins;
			if (sub < m) m = sub;
			curr[j] = m;
		}
		for (size_t j = 0; j <= lb; j++) prev[j] = curr[j];
	}
	return prev[lb];
}

const char *cfg_help_suggest_key(const char *input) {
	if (!input || !*input) return NULL;
	size_t in_len = strlen(input);
	const char *best = NULL;
	size_t best_dist = (size_t)-1;
	for (size_t i = 0; ALL_KNOWN_KEYS[i]; i++) {
		const char *k = ALL_KNOWN_KEYS[i];
		size_t d = edit_distance(input, k);
		if (d < best_dist) {
			best_dist = d;
			best = k;
		}
	}
	if (!best) return NULL;
	size_t max_allowed = in_len / 3 + 1;
	if (max_allowed < 2) max_allowed = 2;
	return best_dist <= max_allowed ? best : NULL;
}

int cfg_help_example_for_key(const char *key, const char **example_out, const char **def_out) {
	*def_out = NULL;
	for (size_t i = 0; i < TOP_EXAMPLES_N; i++) {
		if (strcmp(TOP_EXAMPLES[i].key, key) == 0) {
			*example_out = TOP_EXAMPLES[i].example;
			*def_out = TOP_EXAMPLES[i].def;
			return 0;
		}
	}
	if (strncmp(key, "services.", 9) == 0) {
		const char *rest = key + 9;
		const char *dot = strchr(rest, '.');
		if (!dot) return -1;
		const char *leaf = dot + 1;
		if (strcmp(leaf, "auth") == 0) {
			*example_out = "<api-token>";
			return 0;
		}
		if (strcmp(leaf, "domain") == 0) {
			*example_out = "https://<host>/api/upload";
			return 0;
		}
		if (strcmp(leaf, "folder") == 0) {
			*example_out = "<folder-uuid>";
			return 0;
		}
		if (strncmp(leaf, "headers.", 8) == 0) {
			const struct zl_hdr *h = gcfg_zl_find(leaf + 8);
			if (h) {
				*example_out = zl_header_example(h);
				return 0;
			}
		}
	}
	if (strncmp(key, "recording.", 10) == 0) {
		const char *leaf = key + 10;
		if (strcmp(leaf, "fps") == 0) {
			*example_out = "1-120";
			*def_out = "30";
			return 0;
		}
		if (strcmp(leaf, "crf") == 0) {
			*example_out = "0-51";
			*def_out = "20";
			return 0;
		}
		if (strcmp(leaf, "max_size_mb") == 0) {
			*example_out = "100 (0 to disable)";
			return 0;
		}
		if (strcmp(leaf, "cursor") == 0) {
			*example_out = "true|false";
			*def_out = "true";
			return 0;
		}
		if (strcmp(leaf, "ffmpeg") == 0) {
			*example_out = "ffmpeg | /usr/bin/ffmpeg";
			*def_out = "ffmpeg";
			return 0;
		}
		if (strcmp(leaf, "preset") == 0) {
			*example_out = "ultrafast|superfast|veryfast|faster|fast|medium|slow|slower|veryslow";
			*def_out = "fast";
			return 0;
		}
		if (strcmp(leaf, "tune") == 0) {
			*example_out = "film|animation|grain|stillimage|psnr|ssim|fastdecode|zerolatency (empty to disable)";
			return 0;
		}
		if (strcmp(leaf, "pix_fmt") == 0) {
			*example_out = "yuv420p|yuv422p|yuv444p|yuv420p10le";
			*def_out = "yuv420p";
			return 0;
		}
	}
	if (strncmp(key, "edit.", 5) == 0) {
		const char *leaf = key + 5;
		if (strcmp(leaf, "color") == 0) {
			*example_out = "#RRGGBB or red|yellow|green|blue|black|white";
			*def_out = "#FF3030";
			return 0;
		}
		if (strcmp(leaf, "width") == 0) {
			*example_out = "1..20";
			*def_out = "4";
			return 0;
		}
	}
	if (strncmp(key, "sound.", 6) == 0) {
		const char *leaf = key + 6;
		if (strcmp(leaf, "enabled") == 0) {
			*example_out = "true|false";
			*def_out = "false";
			return 0;
		}
		if (strcmp(leaf, "player") == 0) {
			*example_out = "pw-play | paplay | play | aplay | <abs path>";
			return 0;
		}
		if (strcmp(leaf, "file") == 0) {
			*example_out = "<path to .oga/.wav file>";
			return 0;
		}
	}
	if (strcmp(key, "jpeg.quality") == 0) {
		*example_out = "1..100";
		*def_out = "90";
		return 0;
	}
	if (strcmp(key, "webp.quality") == 0) {
		*example_out = "0..100";
		*def_out = "85";
		return 0;
	}
	if (strcmp(key, "webp.lossless") == 0) {
		*example_out = "true|false";
		*def_out = "false";
		return 0;
	}
	if (strncmp(key, "ocr.", 4) == 0) {
		const char *leaf = key + 4;
		if (strcmp(leaf, "tesseract") == 0) {
			*example_out = "tesseract | /usr/local/bin/tesseract";
			*def_out = "tesseract";
			return 0;
		}
	}
	return -1;
}

bool cfg_help_print_example(const char *example, const char *def) {
	if (!def) {
		printf("%s", example);
		return false;
	}
	size_t deflen = strlen(def);
	const char *p = example;
	bool starred = false;
	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		if (!starred && len == deflen && strncmp(p, def, len) == 0) {
			printf("%.*s*", (int)len, p);
			starred = true;
		} else {
			printf("%.*s", (int)len, p);
		}
		if (!bar) break;
		printf("|");
		p = bar + 1;
	}
	return starred;
}

static void print_key_with_default(const char *key, const char *def) {
	if (def)
		printf("  %-28s default: %s\n", key, def);
	else
		printf("  %s\n", key);
}

static const char *find_default(const char *key) {
	const char *ex = NULL, *def = NULL;
	if (cfg_help_example_for_key(key, &ex, &def) == 0) return def;
	return NULL;
}

void cfg_help_print_all_keys(void) {
	puts("keys (run `grabit set <key>` for example values):");
	puts("");
	for (size_t i = 0; i < TOP_EXAMPLES_N; i++) {
		print_key_with_default(TOP_EXAMPLES[i].key, TOP_EXAMPLES[i].def);
	}
	puts("");
	puts("  services.<svc>.auth     (svc: zipline|nest|fakecrime|ez|guns|pixelvault)");
	puts("  services.zipline.domain");
	puts("  services.nest.folder");
	puts("");
	puts("  services.zipline.headers.<name>:");
	for (size_t i = 0; i < gcfg_zl_headers_n; i++) {
		printf("    %s\n", gcfg_zl_headers[i].name);
	}
	puts("");
	static const char *const RECORDING_KEYS[] = {
		"recording.fps",
		"recording.crf",
		"recording.preset",
		"recording.tune",
		"recording.pix_fmt",
		"recording.max_size_mb",
		"recording.cursor",
		"recording.ffmpeg",
		NULL,
	};
	for (size_t i = 0; RECORDING_KEYS[i]; i++) {
		print_key_with_default(RECORDING_KEYS[i], find_default(RECORDING_KEYS[i]));
	}
	puts("");
	static const char *const SOUND_KEYS[] = {
		"sound.enabled",
		"sound.player",
		"sound.file",
		NULL,
	};
	for (size_t i = 0; SOUND_KEYS[i]; i++) {
		print_key_with_default(SOUND_KEYS[i], find_default(SOUND_KEYS[i]));
	}
	puts("");
	print_key_with_default("edit.color", find_default("edit.color"));
	print_key_with_default("edit.width", find_default("edit.width"));
	puts("");
	static const char *const ENCODER_KEYS[] = {
		"jpeg.quality",
		"webp.quality",
		"webp.lossless",
		NULL,
	};
	for (size_t i = 0; ENCODER_KEYS[i]; i++) {
		print_key_with_default(ENCODER_KEYS[i], find_default(ENCODER_KEYS[i]));
	}
	puts("");
	print_key_with_default("ocr.tesseract", find_default("ocr.tesseract"));
}
