// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_H
#define GRABIT_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct grabit_wl_state;
struct grabit_output;

struct image {
	int32_t  width;
	int32_t  height;
	int32_t  stride;
	uint32_t format;
	void    *bytes;
	size_t   size;
};

void image_free(struct image *img);

int capture_output_full(struct grabit_wl_state *s, struct grabit_output *o,
                        struct image *out);

int capture_output_region(struct grabit_wl_state *s, struct grabit_output *o,
                          int32_t x, int32_t y, int32_t w, int32_t h,
                          bool overlay_cursor,
                          struct image *out);

#endif
