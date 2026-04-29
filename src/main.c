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
#include "capture/freeze.h"
#include "clipboard/clipboard.h"
#include "config.h"
#include "log.h"
#include "mime.h"
#include "notify/notify.h"
#include "ocr/ocr.h"
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
	} else if (eff == ACTION_OCR) {
		save = false;
	} else {
		const char *si = config_get(cfg, "save_captures");
		save = si && strcmp(si, "true") == 0;
	}
	*is_temp = !save;
	enum paths_dest dest = save ? PATHS_DEST_PICTURES : PATHS_DEST_TEMP;
	return paths_build_output(cfg, a->filename_tpl, ".png", dest);
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

	int rc = grabit_freeze_capture(&s, path);
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
	const char *bin = config_get(cfg, "ocr.tesseract");
	if (!bin || !bin[0]) bin = "tesseract";

	if (grabit_ocr_check(bin) != 0) {
		log_error("ocr: tesseract not found in $PATH (or `%s` not executable)", bin);
		log_error("  install tesseract + the english training data:");
		log_error("    void:   xbps-install -S tesseract-ocr tesseract-ocr-eng");
		log_error("    arch:   pacman -S tesseract tesseract-data-eng");
		log_error("    debian: apt install tesseract-ocr tesseract-ocr-eng");
		log_error("    fedora: dnf install tesseract tesseract-langpack-eng");
		log_error("  or set ocr.tesseract to a custom path:");
		log_error("    grabit set ocr.tesseract /usr/local/bin/tesseract");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "tesseract not installed — see terminal for details",
			.force = true,
		});
		return 1;
	}

	bool is_temp = false;
	char *path;
	if (a->file) {
		path = strdup(a->file);
		if (!path) {
			log_error("oom: run_ocr");
			return 1;
		}
	} else {
		path = capture_to_file(a, cfg, ACTION_OCR, &is_temp);
		if (!path) return 1;
	}

	char *text = grabit_ocr_run(bin, path);

	if (is_temp) {
		unlink(path);
		clear_tmpfile();
	}
	free(path);

	if (!text) {
		notify_send(&(struct notify_opts){
			.summary = "OCR failed",
			.body = "see terminal for details",
			.force = true,
		});
		return 1;
	}
	if (!text[0]) {
		free(text);
		log_info("OCR: no text found in selection");
		notify_send(&(struct notify_opts){
			.summary = "OCR: no text found",
			.force = true,
		});
		return 1;
	}

	if (clipboard_set_text(text) != 0) {
		log_error("OCR: clipboard write failed");
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = "OCR text not copied — see terminal for details",
			.force = true,
		});
		free(text);
		return 1;
	}

	size_t tlen = strlen(text);
	char preview[160];
	if (tlen > 100) {
		size_t n = 100;
		while (n > 0 && ((unsigned char)text[n] & 0xC0) == 0x80)
			n--;
		snprintf(preview, sizeof preview, "%.*s…", (int)n, text);
	} else {
		snprintf(preview, sizeof preview, "%s", text);
	}

	log_info("OCR: %zu chars copied to clipboard", tlen);
	notify_send(&(struct notify_opts){
		.summary = "OCR Complete",
		.body = preview,
	});

	free(text);
	return 0;
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
