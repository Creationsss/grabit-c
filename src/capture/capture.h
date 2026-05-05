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
	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
	void *bytes;
	size_t size;
};

void image_free(struct image *img);

int image_apply_transform(struct image *img, int32_t transform);

struct _cairo;
bool grabit_wl_transform_swaps(int32_t transform);
void grabit_wl_transform_apply_inverse(struct _cairo *cr, int32_t transform,
									   int32_t src_w, int32_t src_h);

int grabit_cairo_format_for_shm(uint32_t shm_fmt);

int capture_output_full(struct grabit_wl_state *s, struct grabit_output *o,
						struct image *out);

struct wl_buffer;
struct sc_pool {
	struct wl_buffer *buffer;
	void *map;
	size_t map_size;
	int32_t width, height, stride;
	uint32_t format;
};

void sc_pool_destroy(struct sc_pool *p);

int capture_output_region_into(struct grabit_wl_state *s, struct grabit_output *o,
							   int32_t x, int32_t y, int32_t w, int32_t h,
							   bool overlay_cursor,
							   void *dst, int32_t dst_stride, int32_t dst_h,
							   uint32_t *out_format,
							   struct sc_pool *cache);

#endif
