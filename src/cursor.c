// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "cursor.h"

#include <stdlib.h>

#include <wayland-cursor.h>

#define DEFAULT_CURSOR_SIZE 24
#define MIN_CURSOR_SIZE 8
#define MAX_CURSOR_SIZE 256

struct wl_cursor_theme *grabit_cursor_theme_load(struct wl_shm *shm, int32_t scale) {
	if (!shm) return NULL;
	const char *theme_name = getenv("XCURSOR_THEME");
	int32_t theme_size = DEFAULT_CURSOR_SIZE;
	const char *size_env = getenv("XCURSOR_SIZE");
	if (size_env && *size_env) {
		char *end = NULL;
		long v = strtol(size_env, &end, 10);
		if (end != size_env && v >= MIN_CURSOR_SIZE && v <= MAX_CURSOR_SIZE) {
			theme_size = (int32_t)v;
		}
	}
	if (scale < 1) scale = 1;
	return wl_cursor_theme_load(theme_name, theme_size * scale, shm);
}

struct wl_cursor *grabit_cursor_load_first(struct wl_cursor_theme *theme,
										   const char *const *names) {
	if (!theme || !names) return NULL;
	for (size_t i = 0; names[i]; i++) {
		struct wl_cursor *c = wl_cursor_theme_get_cursor(theme, names[i]);
		if (c) return c;
	}
	return NULL;
}
