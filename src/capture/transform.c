// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/capture.h"

#include "cairo_util.h"
#include "log.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int grabit_cairo_format_for_shm(uint32_t shm_fmt) {
	if (shm_fmt == WL_SHM_FORMAT_ARGB8888) return CAIRO_FORMAT_ARGB32;
	return CAIRO_FORMAT_RGB24;
}

bool grabit_wl_transform_swaps(int32_t transform) {
	return transform == WL_OUTPUT_TRANSFORM_90 ||
		   transform == WL_OUTPUT_TRANSFORM_270 ||
		   transform == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
		   transform == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

void grabit_wl_transform_apply_inverse(cairo_t *cr, int32_t transform,
									   int32_t src_w, int32_t src_h) {
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		break;
	case WL_OUTPUT_TRANSFORM_90:
		cairo_translate(cr, src_h, 0);
		cairo_rotate(cr, M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_180:
		cairo_translate(cr, src_w, src_h);
		cairo_rotate(cr, M_PI);
		break;
	case WL_OUTPUT_TRANSFORM_270:
		cairo_translate(cr, 0, src_w);
		cairo_rotate(cr, -M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED:
		cairo_translate(cr, src_w, 0);
		cairo_scale(cr, -1, 1);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_90:
		cairo_translate(cr, src_h, src_w);
		cairo_scale(cr, -1, 1);
		cairo_rotate(cr, -M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_FLIPPED_180:
		cairo_translate(cr, 0, src_h);
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

	int32_t new_w = grabit_wl_transform_swaps(transform) ? img->height : img->width;
	int32_t new_h = grabit_wl_transform_swaps(transform) ? img->width : img->height;

	cairo_format_t fmt = grabit_cairo_format_for_shm(img->format);
	int32_t new_stride = cairo_format_stride_for_width(fmt, new_w);
	if (new_stride <= 0) return -1;

	size_t new_size = (size_t)new_stride * (size_t)new_h;
	void *new_bytes = malloc(new_size);
	if (!new_bytes) {
		log_error("transform: oom (%zu bytes)", new_size);
		return -1;
	}
	memset(new_bytes, 0, new_size);

	cairo_surface_t *dst = grabit_cairo_image(new_bytes, fmt, new_w, new_h, new_stride);
	cairo_surface_t *src = grabit_cairo_image(img->bytes, fmt, img->width,
											  img->height, img->stride);
	if (!dst || !src) {
		log_error("transform: cairo surface init failed");
		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		free(new_bytes);
		return -1;
	}

	cairo_t *cr = cairo_create(dst);
	grabit_wl_transform_apply_inverse(cr, transform, img->width, img->height);
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
