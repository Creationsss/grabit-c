// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "config.h"

#include "config_internal.h"
#include "log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
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

static int example_for_key(const char *key, const char **example_out, const char **def_out) {
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

static bool print_example(const char *example, const char *def) {
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
	if (example_for_key(key, &ex, &def) == 0) return def;
	return NULL;
}

static void print_set_help(void) {
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
	print_key_with_default("ocr.tesseract", find_default("ocr.tesseract"));
}

int cmd_set(int argc, char **argv) {
	if (argc == 0) {
		print_set_help();
		return 0;
	}
	if (argc == 1) {
		const char *ex = NULL, *def = NULL;
		if (example_for_key(argv[0], &ex, &def) != 0) {
			log_error("unknown key: %s", argv[0]);
			log_info("run `grabit set` to see all keys");
			return 1;
		}
		printf("%s = ", argv[0]);
		bool starred = print_example(ex, def);
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
		c.kvs[i] = c.kvs[--c.n];
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
