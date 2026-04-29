// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "args.h"
#include "capture/capture.h"
#include "capture/png.h"
#include "clipboard/clipboard.h"
#include "config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "paths.h"
#include "record/record.h"
#include "region/region.h"
#include "upload/upload.h"
#include "wl.h"

#ifndef GRABIT_VERSION
#define GRABIT_VERSION "0.1.0"
#endif

static char g_tmpfile_path[4096] = {0};

static void register_tmpfile(const char *path) {
	if (!path) return;
	size_t n = strlen(path);
	if (n >= sizeof g_tmpfile_path) n = sizeof g_tmpfile_path - 1;
	memcpy(g_tmpfile_path, path, n);
	g_tmpfile_path[n] = 0;
}

static void clear_tmpfile(void) {
	g_tmpfile_path[0] = 0;
}

static void on_signal(int sig) {
	if (g_tmpfile_path[0]) unlink(g_tmpfile_path);
	signal(sig, SIG_DFL);
	raise(sig);
}

static void install_signal_handlers(void) {
	struct sigaction sa = {0};
	sa.sa_handler = on_signal;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);
}

static int print_version(void) {
	puts("grabit " GRABIT_VERSION);
	return 0;
}

static int print_help(void) {
	fputs(
		"Usage: grabit [options]\n"
		"\n"
		"Capture & output:\n"
		"  -c                Copy screenshot to clipboard\n"
		"  -u                Upload screenshot to default service\n"
		"  --<service>       Upload to a specific service\n"
		"                    (zipline|nest|fakecrime|ez|guns|pixelvault)\n"
		"  -o, --output, --save\n"
		"                    Capture and print path to stdout\n"
		"  -f <file>         Use <file> instead of taking a screenshot\n"
		"  --tesseract       Capture, OCR, copy text to clipboard\n"
		"  --record          Toggle screen recording (re-run to stop)\n"
		"                    With --save: skip auto-upload even if default_action=upload\n"
		"                    With --<service>: upload to that service after recording\n"
		"  --no-tray         Skip SNI tray during recording\n"
		"  -e, --edit        Open the captured file in an editor first\n"
		"  --silent          Suppress notifications and sound\n"
		"  -d                Enable debug logging to stderr\n"
		"  --filename <tpl>  Per-run filename template\n"
		"\n"
		"Config:\n"
		"  set <key> <val>   Write a config key (validated)\n"
		"  set <key>         Print example value for that key\n"
		"  set               List all available keys\n"
		"  get [<key>]       Print one config key, or every set key\n"
		"  unset <key>       Remove a config key\n"
		"\n"
		"Misc:\n"
		"  --version         Print version and exit\n"
		"  --help            Print this help and exit\n"
		"\n"
		"Environment:\n"
		"  GRABIT_DEBUG=1            Same as -d\n"
		"  GRABIT_<SERVICE>_AUTH     Auth token (overrides config). Recommended:\n"
		"                            export GRABIT_ZIPLINE_AUTH=\"$(pass show grabit/zipline)\"\n",
		stdout);
	return 0;
}

static char *build_capture_path(const struct args *a, struct config *cfg,
								enum action eff, bool *is_temp) {
	bool save;
	if (eff == ACTION_OUTPUT) {
		save = true;
	} else {
		const char *si = config_get(cfg, "save_captures");
		save = si && strcmp(si, "true") == 0;
	}
	*is_temp = !save;
	enum paths_dest dest = save ? PATHS_DEST_PICTURES : PATHS_DEST_TEMP;
	return paths_build_output(cfg, a->filename_tpl, ".png", dest);
}

static int do_freeze_capture(struct grabit_wl_state *s, const char *path) {
	struct image *frozen = calloc(s->n_outputs, sizeof *frozen);
	if (!frozen) return -1;

	int rc = -1;
	size_t captured = 0;
	struct png_slice *slices = NULL;

	for (size_t i = 0; i < s->n_outputs; i++) {
		if (capture_output_full(s, s->outputs[i], &frozen[i]) != 0) {
			log_error("freeze: capture of %s failed",
					  s->outputs[i]->name ? s->outputs[i]->name : "?");
			goto cleanup;
		}
		captured = i + 1;
	}

	struct rect r;
	if (region_select(s, frozen, &r) != 0) {
		log_info("region selection cancelled");
		goto cleanup;
	}
	if (r.w <= 0 || r.h <= 0) {
		log_error("empty selection");
		goto cleanup;
	}

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t lx0 = o->x;
		int32_t ly0 = o->y;
		int32_t lx1 = lx0 + o->logical_width;
		int32_t ly1 = ly0 + o->logical_height;
		if (r.x >= lx1 || r.x + r.w <= lx0) continue;
		if (r.y >= ly1 || r.y + r.h <= ly0) continue;
		if (o->scale > max_scale) max_scale = o->scale;
	}

	int32_t dst_w = r.w * max_scale;
	int32_t dst_h = r.h * max_scale;

	slices = calloc(s->n_outputs, sizeof *slices);
	if (!slices) {
		log_error("oom: composite slices");
		goto cleanup;
	}
	size_t n_slices = 0;

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t lx0 = o->x;
		int32_t ly0 = o->y;
		int32_t lx1 = lx0 + o->logical_width;
		int32_t ly1 = ly0 + o->logical_height;

		int32_t ix0 = r.x > lx0 ? r.x : lx0;
		int32_t iy0 = r.y > ly0 ? r.y : ly0;
		int32_t ix1 = (r.x + r.w) < lx1 ? (r.x + r.w) : lx1;
		int32_t iy1 = (r.y + r.h) < ly1 ? (r.y + r.h) : ly1;
		int32_t iw = ix1 - ix0;
		int32_t ih = iy1 - iy0;
		if (iw <= 0 || ih <= 0) continue;

		// buffer/logical ratio; wlr-screencopy returns post-transform buffer dims, so this stays correct for rotated/flipped/scaled outputs.
		double sxr = o->logical_width > 0
						 ? (double)frozen[i].width / (double)o->logical_width
						 : 1.0;
		double syr = o->logical_height > 0
						 ? (double)frozen[i].height / (double)o->logical_height
						 : 1.0;

		struct png_slice *sl = &slices[n_slices++];
		sl->src = &frozen[i];
		sl->src_x = (int32_t)((ix0 - lx0) * sxr + 0.5);
		sl->src_y = (int32_t)((iy0 - ly0) * syr + 0.5);
		sl->src_w = (int32_t)(iw * sxr + 0.5);
		sl->src_h = (int32_t)(ih * syr + 0.5);
		if (sl->src_x + sl->src_w > frozen[i].width)
			sl->src_w = frozen[i].width - sl->src_x;
		if (sl->src_y + sl->src_h > frozen[i].height)
			sl->src_h = frozen[i].height - sl->src_y;

		sl->dst_x = (ix0 - r.x) * max_scale;
		sl->dst_y = (iy0 - r.y) * max_scale;
		sl->dst_w = iw * max_scale;
		sl->dst_h = ih * max_scale;
	}

	if (n_slices == 0) {
		log_error("selection doesn't intersect any output");
		goto cleanup;
	}

	rc = grabit_png_write_composite(dst_w, dst_h, slices, n_slices, path);

cleanup:
	free(slices);
	for (size_t i = 0; i < captured; i++)
		image_free(&frozen[i]);
	free(frozen);
	return rc;
}

static char *capture_to_file(const struct args *a, struct config *cfg,
							 enum action eff, bool *is_temp) {
	*is_temp = false;
	char *path = build_capture_path(a, cfg, eff, is_temp);
	if (!path) return NULL;

	if (*is_temp) register_tmpfile(path);

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "could not connect to wayland compositor",
			.force = true,
		});
		free(path);
		clear_tmpfile();
		return NULL;
	}

	int rc = do_freeze_capture(&s, path);
	grabit_wl_finish(&s);

	if (rc != 0) {
		unlink(path);
		clear_tmpfile();
		free(path);
		return NULL;
	}
	log_debug("captured to %s", path);
	return path;
}

static int run_upload(struct config *cfg, const struct args *a) {
	const char *service = NULL;
	if (upload_preflight(cfg, a, &service) != 0) return 1;

	bool is_temp = false;
	char *path = NULL;

	if (a->file) {
		path = strdup(a->file);
		if (!path) {
			log_error("oom: run_upload");
			return 1;
		}
	} else {
		path = capture_to_file(a, cfg, ACTION_UPLOAD, &is_temp);
		if (!path) return 1;
	}

	struct upload_result r = {0};
	int rc = upload_perform(service, path, cfg, &r);

	if (rc == 0) {
		clipboard_set_text(r.url);
		char *m = mime_for_file(path);
		const char *summary = mime_is_video(m) ? "Video uploaded" : "Uploaded";
		struct notify_opts opts = {
			.summary = summary,
			.body = r.url,
			.icon_path = mime_is_image(m) ? path : NULL,
		};
		notify_send(&opts);
		free(m);
		log_info("%s", r.url);
	} else {
		char body_short[1024];
		const char *body = r.body;
		if (body && strlen(body) >= sizeof body_short) {
			memcpy(body_short, body, sizeof body_short - 4);
			memcpy(body_short + sizeof body_short - 4, "...", 4);
			body = body_short;
		}
		struct notify_opts opts = {
			.summary = "Upload failed",
			.body = body,
			.force = true,
		};
		notify_send(&opts);
	}

	upload_result_free(&r);
	if (is_temp) {
		unlink(path);
		clear_tmpfile();
	}
	free(path);
	return rc == 0 ? 0 : 1;
}

static int run_copy(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	char *path;
	if (a->file) {
		path = strdup(a->file);
		if (!path) {
			log_error("oom: run_copy");
			return 1;
		}
	} else {
		path = capture_to_file(a, cfg, ACTION_COPY, &is_temp);
		if (!path) return 1;
	}

	int rc = clipboard_set_image_file(path);

	if (rc == 0) {
		notify_send(&(struct notify_opts){
			.summary = "Copied to clipboard",
			.body = path,
			.icon_path = path,
		});
	} else {
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = path,
			.force = true,
		});
	}

	if (is_temp) {
		unlink(path);
		clear_tmpfile();
	}
	free(path);
	return rc == 0 ? 0 : 1;
}

static int run_output(struct config *cfg, const struct args *a) {
	if (a->file) {
		puts(a->file);
		return 0;
	}
	bool is_temp = false;
	char *path = capture_to_file(a, cfg, ACTION_OUTPUT, &is_temp);
	if (!path) return 1;

	puts(path);
	notify_send(&(struct notify_opts){
		.summary = "Saved",
		.body = path,
		.icon_path = path,
	});
	free(path);
	return 0;
}

static int run_ocr(struct config *cfg, const struct args *a) {
	(void)cfg;
	(void)a;
	log_error("--tesseract not yet implemented.");
	notify_send(&(struct notify_opts){
		.summary = "grabit",
		.body = "--tesseract not yet implemented",
		.force = true,
	});
	return 1;
}

static int run_record(struct config *cfg, const struct args *a) {
	return record_toggle(cfg, a);
}

static int run(const struct args *a) {
	struct config cfg;
	if (config_load(&cfg) != 0) return 1;
	notify_init(&cfg, a->silent);

	enum action eff = a->action;
	if (eff == ACTION_NONE) {
		const char *def = config_get(&cfg, "default_action");
		if (def && strcmp(def, "upload") == 0)
			eff = ACTION_UPLOAD;
		else if (def && strcmp(def, "copy") == 0)
			eff = ACTION_COPY;
		else if (def && strcmp(def, "save") == 0)
			eff = ACTION_OUTPUT;
	}

	int rc;
	switch (eff) {
	case ACTION_UPLOAD:
		rc = run_upload(&cfg, a);
		break;
	case ACTION_COPY:
		rc = run_copy(&cfg, a);
		break;
	case ACTION_OUTPUT:
		rc = run_output(&cfg, a);
		break;
	case ACTION_OCR:
		rc = run_ocr(&cfg, a);
		break;
	case ACTION_RECORD:
		rc = run_record(&cfg, a);
		break;
	default:
		log_error("no action specified — try -u, -c, -o, --record, or --tesseract");
		log_info("(or set a default with: grabit set default_action upload)");
		rc = 1;
		break;
	}

	config_free(&cfg);
	return rc;
}

int main(int argc, char **argv) {
	bool pre_silent = false, pre_debug = false;
	args_pre_scan(argc, argv, &pre_silent, &pre_debug);
	log_init(pre_silent, pre_debug);
	install_signal_handlers();

	if (argc >= 2) {
		const char *first = argv[1];
		if (strcmp(first, "--version") == 0) return print_version();
		if (strcmp(first, "--help") == 0 || strcmp(first, "-h") == 0) return print_help();
		if (strcmp(first, "set") == 0) return cmd_set(argc - 2, argv + 2);
		if (strcmp(first, "get") == 0) return cmd_get(argc - 2, argv + 2);
		if (strcmp(first, "unset") == 0) return cmd_unset(argc - 2, argv + 2);
	}

	struct args a;
	if (args_parse(argc, argv, &a) != 0) return 1;
	return run(&a);
}
