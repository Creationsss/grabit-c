// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include <errno.h>
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
#include "pin/pin.h"
#include "plugin/plugin.h"
#include "record/record.h"
#include "region/region.h"
#include "sound/sound.h"
#include "upload/upload.h"
#include "util.h"
#include "wl.h"

#ifndef GRABIT_VERSION
#define GRABIT_VERSION "0.2.0"
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
		"  --pin             Capture and pin to desktop (click-through; stack any number)\n"
		"  --grab            Make all pinned screenshots clickable (click closes that one)\n"
		"  --release         Restore pinned screenshots to click-through\n"
		"  --close-all       Dismiss every pinned screenshot\n"
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
		"Custom uploaders (ShareX .sxcu):\n"
		"  sxcu add <file>   Register a .sxcu uploader (use as --<name>)\n"
		"  sxcu list         Show registered uploaders\n"
		"  sxcu show <name>  Print parsed fields\n"
		"  sxcu rm <name>    Remove an uploader\n"
		"\n"
		"Plugins:\n"
		"  plugin install <git-url>  Clone, build, and install a plugin\n"
		"  plugin list               List installed plugins\n"
		"  plugin show <name>        Print parsed manifest\n"
		"  plugin update [<name>]    Update one plugin or all\n"
		"  plugin remove <name>      Uninstall a plugin\n"
		"  <name> ...                Run installed plugin `grabit-<name>`\n"
		"                            (auto-updates in background per manifest)\n"
		"\n"
		"Filename templates (--filename or `filename` config key):\n"
		"  %Y %m %d %H %M %S strftime fields\n"
		"  %s                unix timestamp\n"
		"  %r[N]             random alphanumeric, N chars (default 12)\n"
		"  %u                uuid v4\n"
		"  %w                active window class (hyprland)\n"
		"  %t                active window title (hyprland)\n"
		"  %o                output name where the capture happened\n"
		"\n"
		"With no action and no `default_action` set, grabit prints this help.\n"
		"Set one with: grabit set default_action upload|copy|save|pin\n"
		"\n"
		"Misc:\n"
		"  --version         Print version and exit\n"
		"  -h, --help        Print this help and exit\n"
		"\n"
		"Environment:\n"
		"  GRABIT_DEBUG=1            Same as -d\n"
		"  GRABIT_<SERVICE>_AUTH     Auth token (overrides config). Service is one of\n"
		"                            ZIPLINE, NEST, FAKECRIME, EZ, GUNS, PIXELVAULT.\n"
		"                            Example: export GRABIT_ZIPLINE_AUTH=\"$(pass show grabit/zipline)\"\n",
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

static const struct {
	const char *name;
	uint32_t hex;
} EDIT_COLORS[] = {
	{"red", 0xff3030u},
	{"yellow", 0xfff030u},
	{"green", 0x40ff40u},
	{"blue", 0x4080ffu},
	{"black", 0x000000u},
	{"white", 0xffffffu},
};

static int hex_nybble(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static uint32_t edit_color_from_str(const char *s) {
	if (!s || !*s) return 0xff3030u;
	const char *p = (*s == '#') ? s + 1 : s;
	size_t len = strlen(p);
	if (len == 6 || len == 3) {
		uint32_t v = 0;
		for (size_t i = 0; i < len; i++) {
			int d = hex_nybble(p[i]);
			if (d < 0) goto try_name;
			v = (v << 4) | (uint32_t)d;
		}
		if (len == 3) {
			uint32_t r = (v >> 8) & 0xf, g = (v >> 4) & 0xf, b = v & 0xf;
			v = (r << 20) | (r << 16) | (g << 12) | (g << 8) | (b << 4) | b;
		}
		return v & 0xFFFFFFu;
	}
try_name:
	for (size_t i = 0; i < sizeof EDIT_COLORS / sizeof EDIT_COLORS[0]; i++) {
		if (strcmp(EDIT_COLORS[i].name, s) == 0) return EDIT_COLORS[i].hex;
	}
	return 0xff3030u;
}

static void edit_color_to_str(uint32_t hex, char *buf, size_t cap) {
	snprintf(buf, cap, "#%06X", hex & 0xFFFFFFu);
}

static int32_t edit_width_from_str(const char *s) {
	if (!s) return 4;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s || v < 1 || v > 20) return 4;
	return (int32_t)v;
}

static void persist_edit_choices(struct config *cfg, uint32_t color, int32_t width) {
	char cn[10];
	edit_color_to_str(color, cn, sizeof cn);
	char wn[16];
	snprintf(wn, sizeof wn, "%d", width);
	const char *cur_c = config_get(cfg, "edit.color");
	const char *cur_w = config_get(cfg, "edit.width");
	bool changed = false;
	if (!cur_c || strcmp(cur_c, cn) != 0) {
		if (config_set(cfg, "edit.color", cn) == 0) changed = true;
	}
	if (!cur_w || strcmp(cur_w, wn) != 0) {
		if (config_set(cfg, "edit.width", wn) == 0) changed = true;
	}
	if (changed) (void)config_save(cfg);
}

static char *capture_to_file(const struct args *a, struct config *cfg,
							 enum action eff, bool *is_temp,
							 struct rect *out_rect) {
	*is_temp = false;
	char *path = build_capture_path(a, cfg, eff, is_temp);
	if (!path) {
		notify_send(&(struct notify_opts){
			.summary = "grabit: capture failed",
			.body = "could not build output path; see terminal for details",
			.force = true,
		});
		return NULL;
	}

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

	uint32_t edit_color = edit_color_from_str(config_get(cfg, "edit.color"));
	int32_t edit_width = edit_width_from_str(config_get(cfg, "edit.width"));
	bool edit_dirty = false;

	int rc = grabit_freeze_capture(&s, path, out_rect, a->edit,
								   a->edit ? &edit_color : NULL,
								   a->edit ? &edit_width : NULL,
								   a->edit ? &edit_dirty : NULL);
	grabit_wl_finish(&s);

	if (a->edit && edit_dirty) persist_edit_choices(cfg, edit_color, edit_width);

	if (rc != 0) {
		unlink(path);
		clear_tmpfile();
		free(path);
		notify_send(&(struct notify_opts){
			.summary = "grabit: capture failed",
			.body = "selection cancelled or did not intersect any output",
			.force = true,
		});
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
		path = capture_to_file(a, cfg, ACTION_UPLOAD, &is_temp, NULL);
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
			.body = "link copied to clipboard",
			.icon_path = mime_is_image(m) ? path : NULL,
		};
		notify_send(&opts);
		grabit_sound_play(cfg);
		free(m);
		log_info("%s", r.url);
	} else {
		char body_short[1024];
		const char *body = r.body;
		if (body && strlen(body) >= sizeof body_short) {
			static const char trail[] = "… (full response in terminal)";
			size_t keep = sizeof body_short - sizeof trail;
			memcpy(body_short, body, keep);
			memcpy(body_short + keep, trail, sizeof trail);
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
		path = capture_to_file(a, cfg, ACTION_COPY, &is_temp, NULL);
		if (!path) return 1;
	}

	int rc = clipboard_set_image_file(path);

	if (rc == 0) {
		notify_send(&(struct notify_opts){
			.summary = "Copied to clipboard",
			.body = grabit_basename(path),
			.icon_path = path,
		});
		grabit_sound_play(cfg);
	} else {
		notify_send(&(struct notify_opts){
			.summary = "Clipboard write failed",
			.body = grabit_basename(path),
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
	char *path = capture_to_file(a, cfg, ACTION_OUTPUT, &is_temp, NULL);
	if (!path) return 1;

	puts(path);
	if (isatty(STDOUT_FILENO)) {
		notify_send(&(struct notify_opts){
			.summary = "Saved",
			.body = grabit_basename(path),
			.icon_path = path,
		});
		grabit_sound_play(cfg);
	}
	free(path);
	return 0;
}

static int run_ocr(struct config *cfg, const struct args *a) {
	const char *bin = config_get(cfg, "ocr.tesseract");
	if (bin && bin[0] && grabit_ocr_check(bin) != 0) {
		log_error("ocr: configured ocr.tesseract `%s` not found; "
				  "unset with: grabit unset ocr.tesseract",
				  bin);
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "configured tesseract not found; see terminal for details",
			.force = true,
		});
		return 1;
	}
	if (!bin || !bin[0]) {
		static const char *const CANDIDATES[] = {"tesseract", "tesseract-ocr", NULL};
		for (size_t i = 0; CANDIDATES[i]; i++) {
			if (grabit_ocr_check(CANDIDATES[i]) == 0) {
				bin = CANDIDATES[i];
				break;
			}
		}
	}
	if (!bin) {
		log_error("ocr: tesseract not found in $PATH (install tesseract)");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "tesseract not installed",
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
		path = capture_to_file(a, cfg, ACTION_OCR, &is_temp, NULL);
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
			.body = "tesseract returned no output; check that eng.traineddata is installed",
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
			.body = "OCR text not copied; see terminal for details",
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
	grabit_sound_play(cfg);

	free(text);
	return 0;
}

static int run_record(struct config *cfg, const struct args *a) {
	return record_toggle(cfg, a);
}

static int run_pin(struct config *cfg, const struct args *a) {
	bool is_temp = false;
	char *path;
	struct rect r = {0};
	bool have_rect = false;
	if (a->file) {
		path = strdup(a->file);
		if (!path) {
			log_error("oom: run_pin");
			return 1;
		}
	} else {
		path = capture_to_file(a, cfg, ACTION_PIN, &is_temp, &r);
		if (!path) return 1;
		have_rect = (r.w > 0 && r.h > 0);
	}

	int rc = pin_spawn(cfg, path, have_rect ? &r : NULL);

	if (rc == 0) grabit_sound_play(cfg);

	if (is_temp) {
		unlink(path);
		clear_tmpfile();
	}
	free(path);
	return rc == 0 ? 0 : 1;
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
		else if (def && strcmp(def, "pin") == 0)
			eff = ACTION_PIN;
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
	case ACTION_PIN:
		rc = run_pin(&cfg, a);
		break;
	case ACTION_PIN_GRAB:
		rc = pin_grab();
		break;
	case ACTION_PIN_RELEASE:
		rc = pin_release();
		break;
	case ACTION_PIN_CLOSE_ALL:
		rc = pin_close_all();
		break;
	default:
		log_error("no action specified; try -u, -c, -o, --pin, --record, or --tesseract");
		log_info("(or set a default with: grabit set default_action upload)");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "no action; run `grabit set default_action upload|copy|save|pin`",
			.force = true,
		});
		rc = 1;
		break;
	}

	config_free(&cfg);
	return rc;
}

static int try_dispatch_plugin(const char *name, int argc, char **argv) {
	if (!plugin_name_is_valid(name)) return -1;
	char path[1024];
	if (plugin_resolve(name, path, sizeof path) != 0) return -1;

	plugin_maybe_auto_update(name);

	char **new_argv = calloc((size_t)argc + 1, sizeof *new_argv);
	if (!new_argv) return -1;
	new_argv[0] = path;
	for (int i = 1; i < argc; i++) new_argv[i] = argv[i];

	execv(path, new_argv);
	int err = errno;
	free(new_argv);
	log_error("plugin: exec %s: %s", path, strerror(err));
	return -1;
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
		if (strcmp(first, "sxcu") == 0) return cmd_sxcu(argc - 2, argv + 2);
		if (strcmp(first, "plugin") == 0) return cmd_plugin(argc - 2, argv + 2);
		if (first[0] != '-' && try_dispatch_plugin(first, argc - 1, argv + 1) == 0) return 0;
	}

	struct args a;
	if (args_parse(argc, argv, &a) != 0) return 1;
	return run(&a);
}
