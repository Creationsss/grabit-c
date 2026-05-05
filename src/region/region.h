// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_REGION_H
#define GRABIT_REGION_REGION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct grabit_wl_state;
struct image;

struct rect {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
};

#define ANNO_DEFAULT_FONT 18

enum tool_kind {
	TOOL_PEN = 0,
	TOOL_RECT,
	TOOL_ELLIPSE,
	TOOL_ARROW,
	TOOL_BLUR,
	TOOL_TEXT,
	TOOL_ERASER,
	TOOL_COUNT,
};

struct annotation {
	enum tool_kind tool;
	int32_t x0, y0, x1, y1;
	int32_t *points;
	size_t n_points;
	char *text;
	uint32_t color;
	int32_t width;
	int32_t font_size;
};

struct annotation_list {
	struct annotation *items;
	size_t n;
	size_t cap;
};

void annotation_list_free(struct annotation_list *list);

int region_select(struct grabit_wl_state *s, const struct image *frozen_per_output,
				  bool annotate_mode, struct rect *out,
				  struct annotation_list *out_annos,
				  uint32_t *inout_color, int32_t *inout_width,
				  bool *out_choices_dirty);

#endif
