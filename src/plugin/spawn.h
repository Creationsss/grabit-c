// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_SPAWN_H
#define GRABIT_PLUGIN_SPAWN_H

#include <stddef.h>

int plugin_run_in(const char *cwd, char *const argv[]);
int plugin_read_first_line(const char *path, char *out, size_t cap);

#endif
