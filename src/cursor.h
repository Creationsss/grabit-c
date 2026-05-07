// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CURSOR_H
#define GRABIT_CURSOR_H

#include <stdint.h>

struct wl_shm;
struct wl_cursor_theme;
struct wl_cursor;

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale);
struct wl_cursor *grabit_cursor_load_first(struct wl_cursor_theme *theme,
										   const char *const *names);

#endif
