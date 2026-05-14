// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "args.h"

#include "log.h"
#include "upload/upload.h"

#include <stdbool.h>
#include <string.h>

void args_pre_scan(int argc, char **argv, bool *silent, bool *debug) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--silent") == 0)
			*silent = true;
		else if (strcmp(argv[i], "-d") == 0)
			*debug = true;
	}
}

static int set_action(struct args *a, enum action act, const char *flag) {
	if (a->action == act) return 0;
	if (a->action != ACTION_NONE) {
		log_error("conflicting actions: %s contradicts an earlier flag", flag);
		log_info("hint: --record + --no-upload skips the auto-upload after recording");
		return -1;
	}
	a->action = act;
	return 0;
}

int args_parse(int argc, char **argv, struct args *out) {
	memset(out, 0, sizeof *out);

	bool positional_only = false;
	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (!positional_only && strcmp(arg, "--") == 0) {
			positional_only = true;
			continue;
		}
		if (positional_only) {
			if (out->file) {
				log_error("unexpected extra argument: %s", arg);
				return -1;
			}
			out->file = arg;
			if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
			continue;
		}

		if (strcmp(arg, "-c") == 0 || strcmp(arg, "--copy") == 0) {
			if (set_action(out, ACTION_COPY, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "-u") == 0 || strcmp(arg, "--upload") == 0) {
			if (set_action(out, ACTION_UPLOAD, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "-o") == 0 ||
			strcmp(arg, "--output") == 0 ||
			strcmp(arg, "--save") == 0) {
			if (set_action(out, ACTION_OUTPUT, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--tesseract") == 0) {
			if (set_action(out, ACTION_OCR, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--record") == 0) {
			if (set_action(out, ACTION_RECORD, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--pin") == 0) {
			if (set_action(out, ACTION_PIN, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--grab") == 0) {
			if (set_action(out, ACTION_PIN_GRAB, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--release") == 0) {
			if (set_action(out, ACTION_PIN_RELEASE, arg) != 0) return -1;
			continue;
		}
		if (strcmp(arg, "--close-all") == 0) {
			if (set_action(out, ACTION_PIN_CLOSE_ALL, arg) != 0) return -1;
			continue;
		}

		if (strcmp(arg, "-e") == 0 ||
			strcmp(arg, "--edit") == 0) {
			out->edit = true;
			continue;
		}
		if (strcmp(arg, "--no-tray") == 0) {
			out->no_tray = true;
			continue;
		}
		if (strcmp(arg, "--no-upload") == 0) {
			out->no_upload = true;
			continue;
		}
		if (strcmp(arg, "--silent") == 0 ||
			strcmp(arg, "--quiet") == 0 ||
			strcmp(arg, "-q") == 0) {
			out->silent = true;
			continue;
		}
		if (strcmp(arg, "-d") == 0 || strcmp(arg, "--debug") == 0) {
			out->debug = true;
			continue;
		}

		if (strcmp(arg, "-f") == 0) {
			if (++i >= argc) {
				log_error("-f requires a file argument");
				return -1;
			}
			out->file = argv[i];
			if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
			continue;
		}

		if (strcmp(arg, "--filename") == 0) {
			if (++i >= argc) {
				log_error("--filename requires a template argument");
				return -1;
			}
			if (!argv[i][0]) {
				log_error("--filename requires a non-empty template");
				return -1;
			}
			out->filename_tpl = argv[i];
			continue;
		}
		if (strncmp(arg, "--filename=", 11) == 0) {
			out->filename_tpl = arg + 11;
			if (!*out->filename_tpl) {
				log_error("--filename requires a template argument");
				return -1;
			}
			continue;
		}

		if (strcmp(arg, "--format") == 0) {
			if (++i >= argc) {
				log_error("--format requires a value (png|jpeg|webp)");
				return -1;
			}
			if (!argv[i][0]) {
				log_error("--format requires a non-empty value (png|jpeg|webp)");
				return -1;
			}
			out->format = argv[i];
			continue;
		}
		if (strncmp(arg, "--format=", 9) == 0) {
			out->format = arg + 9;
			if (!*out->format) {
				log_error("--format requires a non-empty value (png|jpeg|webp)");
				return -1;
			}
			continue;
		}

		if (arg[0] == '-' && arg[1] == '-' && arg[2] != '\0') {
			const char *name = arg + 2;
			if (upload_service_known(name)) {
				if (out->service && strcmp(out->service, name) != 0) {
					log_error("conflicting services: %s vs %s", out->service, name);
					return -1;
				}
				out->service = name;
				if (out->action == ACTION_NONE) out->action = ACTION_UPLOAD;
				continue;
			}
		}

		log_error("unknown argument: %s (try `grabit --help`)", arg);
		return -1;
	}

	if (out->service && out->action != ACTION_UPLOAD &&
		out->action != ACTION_RECORD && out->action != ACTION_NONE) {
		log_error("--%s only makes sense with -u or --record", out->service);
		return -1;
	}

	if (out->action == ACTION_RECORD && out->file) {
		log_error("--record cannot be combined with -f");
		return -1;
	}

	if (out->edit) {
		bool edit_applies = out->action == ACTION_UPLOAD ||
							out->action == ACTION_COPY ||
							out->action == ACTION_OUTPUT ||
							out->action == ACTION_PIN ||
							out->action == ACTION_NONE;
		if (!edit_applies) log_warn("--edit is ignored for this action");
	}
	if (out->no_tray && out->action != ACTION_RECORD && out->action != ACTION_NONE) {
		log_warn("--no-tray only applies to --record");
	}
	if (out->file && out->format) {
		log_warn("--format is ignored when -f is used (file is uploaded as-is)");
	}
	if (out->file && out->filename_tpl) {
		log_warn("--filename is ignored when -f is used");
	}

	return 0;
}
