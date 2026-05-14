// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool g_silent;
static bool g_debug;
static bool g_color;

static const char *C_RED = "";
static const char *C_YELLOW = "";
static const char *C_CYAN = "";
static const char *C_RESET = "";

bool log_is_silent(void) {
	return g_silent;
}

void log_init(bool silent, bool debug) {
	g_silent = silent;
	g_debug = debug;

	const char *env = getenv("GRABIT_DEBUG");
	if (env && env[0] && strcmp(env, "0") != 0) {
		g_debug = true;
	}

	g_color = isatty(STDERR_FILENO) && getenv("NO_COLOR") == NULL;
	if (g_color) {
		C_RED = "\033[31m";
		C_YELLOW = "\033[33m";
		C_CYAN = "\033[36m";
		C_RESET = "\033[0m";
	}
}

static void emit(const char *prefix, const char *color, const char *fmt, va_list ap) {
	fprintf(stderr, "%s%s%s ", color, prefix, C_RESET);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
}

void log_debug(const char *fmt, ...) {
	if (!g_debug) return;
	va_list ap;
	va_start(ap, fmt);
	emit("[debug]", C_CYAN, fmt, ap);
	va_end(ap);
}

void log_info(const char *fmt, ...) {
	if (g_silent) return;
	va_list ap;
	va_start(ap, fmt);
	emit("[info]", "", fmt, ap);
	va_end(ap);
}

void log_warn(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[warn]", C_YELLOW, fmt, ap);
	va_end(ap);
}

void log_error(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[error]", C_RED, fmt, ap);
	va_end(ap);
}

void die(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	emit("[error]", C_RED, fmt, ap);
	va_end(ap);
	exit(1);
}
