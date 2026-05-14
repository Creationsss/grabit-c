// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_ARGS_H
#define GRABIT_ARGS_H

#include <stdbool.h>

enum action {
	ACTION_NONE,
	ACTION_UPLOAD,
	ACTION_COPY,
	ACTION_OUTPUT,
	ACTION_OCR,
	ACTION_RECORD,
	ACTION_PIN,
	ACTION_PIN_GRAB,
	ACTION_PIN_RELEASE,
	ACTION_PIN_CLOSE_ALL,
};

struct args {
	enum action action;
	bool silent;
	bool debug;
	bool edit;
	bool no_tray;
	bool no_upload;
	const char *file;
	const char *service;
	const char *filename_tpl;
	const char *format;
};

void args_pre_scan(int argc, char **argv, bool *silent, bool *debug);
int args_parse(int argc, char **argv, struct args *out);

#endif
