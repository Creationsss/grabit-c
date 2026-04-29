// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/capture.h"

#include "log.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static cairo_format_t fmt_for(uint32_t f) {
	if (f == WL_SHM_FORMAT_ARGB8888) return CAIRO_FORMAT_ARGB32;
	return CAIRO_FORMAT_RGB24;
}

static int transform_swaps(int32_t t) {
	return t == WL_OUTPUT_TRANSFORM_90 ||
		   t == WL_OUTPUT_TRANSFORM_270 ||
		   t == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
		   t == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

static void apply_inverse(cairo_t *cr, int32_t t, int32_t sw, int32_t sh) {
	switch (t) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		break;
	case WL_OUTPUT_TRANSFORM_90:
		cairo_translate(cr, sh, 0);
		cairo_rotate(cr, M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_180:
		cairo_translate(cr, sw, sh);
		cairo_rotate(cr, M_PI);
		break;
	case WL_OUTPUT_TRANSFORM_270:
		cairo_translate(cr, 0, sw);
		cairo_rotate(cr, -M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		cairo_translate(cr, sw, 0);
		cairo_scale(cr, -1, 1);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		cairo_translate(cr, sh, sw);
		cairo_scale(cr, -1, 1);
		cairo_rotate(cr, -M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		cairo_translate(cr, 0, sh);
		cairo_scale(cr, 1, -1);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_270:
		cairo_rotate(cr, M_PI / 2);
		cairo_scale(cr, 1, -1);
		break;
	}
}

int image_apply_transform(struct image *img, int32_t transform) {
	if (!img || !img->bytes) return -1;
	if (transform == WL_OUTPUT_TRANSFORM_NORMAL) return 0;

	int32_t new_w = transform_swaps(transform) ? img->height : img->width;
	int32_t new_h = transform_swaps(transform) ? img->width : img->height;

	cairo_format_t fmt = fmt_for(img->format);
	int32_t new_stride = cairo_format_stride_for_width(fmt, new_w);
	if (new_stride <= 0) return -1;

	size_t new_size = (size_t)new_stride * (size_t)new_h;
	void *new_bytes = malloc(new_size);
	if (!new_bytes) {
		log_error("transform: oom (%zu bytes)", new_size);
		return -1;
	}
	memset(new_bytes, 0, new_size);

	cairo_surface_t *dst = cairo_image_surface_create_for_data(
		new_bytes, fmt, new_w, new_h, new_stride);
	cairo_surface_t *src = cairo_image_surface_create_for_data(
		img->bytes, fmt, img->width, img->height, img->stride);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS ||
		cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		log_error("transform: cairo surface init failed");
		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		free(new_bytes);
		return -1;
	}

	cairo_t *cr = cairo_create(dst);
	apply_inverse(cr, transform, img->width, img->height);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, src, 0, 0);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);
	cairo_surface_destroy(src);

	free(img->bytes);
	img->bytes = new_bytes;
	img->width = new_w;
	img->height = new_h;
	img->stride = new_stride;
	img->size = new_size;
	return 0;
}
