// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "args.h"

#include "log.h"
#include "upload/upload.h"

#include <stdbool.h>
#include <string.h>

void args_pre_scan(int argc, char **argv, bool *silent, bool *debug) {
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--silent") == 0) *silent = true;
		else if (strcmp(argv[i], "-d") == 0)  *debug = true;
	}
}

static int set_action(struct args *a, enum action act, const char *flag) {
	if (a->action != ACTION_NONE && a->action != act) {
		log_error("conflicting actions: %s contradicts an earlier flag", flag);
		return -1;
	}
	a->action = act;
	return 0;
}

int args_parse(int argc, char **argv, struct args *out) {
	memset(out, 0, sizeof *out);

	for (int i = 1; i < argc; i++) {
		const char *arg = argv[i];

		if (strcmp(arg, "-c") == 0)               { if (set_action(out, ACTION_COPY,   arg) != 0) return -1; continue; }
		if (strcmp(arg, "-u") == 0)               { if (set_action(out, ACTION_UPLOAD, arg) != 0) return -1; continue; }
		if (strcmp(arg, "-o") == 0 ||
		    strcmp(arg, "--output") == 0)         { if (set_action(out, ACTION_OUTPUT, arg) != 0) return -1; continue; }
		if (strcmp(arg, "--tesseract") == 0)      { if (set_action(out, ACTION_OCR,    arg) != 0) return -1; continue; }
		if (strcmp(arg, "--record") == 0)         { if (set_action(out, ACTION_RECORD, arg) != 0) return -1; continue; }

		if (strcmp(arg, "-e") == 0 ||
		    strcmp(arg, "--edit") == 0)           { out->edit = true; continue; }
		if (strcmp(arg, "--no-tray") == 0)        { out->no_tray = true; continue; }
		if (strcmp(arg, "--silent") == 0)         { out->silent = true; continue; }
		if (strcmp(arg, "-d") == 0)               { out->debug = true; continue; }

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

	return 0;
}
