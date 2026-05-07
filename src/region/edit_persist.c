// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/edit_persist.h"

#include "config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EDIT_DEFAULT_COLOR 0xff3030u
#define EDIT_DEFAULT_WIDTH 4
#define EDIT_MIN_WIDTH 1
#define EDIT_MAX_WIDTH 20

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

uint32_t edit_color_from_str(const char *s) {
	if (!s || !*s) return EDIT_DEFAULT_COLOR;
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
	return EDIT_DEFAULT_COLOR;
}

void edit_color_to_str(uint32_t hex, char *buf, size_t cap) {
	snprintf(buf, cap, "#%06X", hex & 0xFFFFFFu);
}

int32_t edit_width_from_str(const char *s) {
	if (!s) return EDIT_DEFAULT_WIDTH;
	char *end = NULL;
	long v = strtol(s, &end, 10);
	if (end == s || v < EDIT_MIN_WIDTH || v > EDIT_MAX_WIDTH) return EDIT_DEFAULT_WIDTH;
	return (int32_t)v;
}

void persist_edit_choices(struct config *cfg, uint32_t color, int32_t width) {
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
