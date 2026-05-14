// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_LOG_H
#define GRABIT_LOG_H

#include <stdbool.h>

void log_init(bool silent, bool debug);
bool log_is_silent(void);

void log_debug(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_info(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_warn(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

__attribute__((noreturn, format(printf, 1, 2))) void die(const char *fmt, ...);

#endif
