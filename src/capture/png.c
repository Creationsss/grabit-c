// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/png.h"

#include "capture/capture.h"
#include "log.h"
#include "util.h"

#include <cairo/cairo.h>
#include <wayland-client.h>

int grabit_png_write(const struct image *img, const char *path) {
	if (!img || !img->bytes || !path) return -1;

	cairo_format_t fmt;
	switch (img->format) {
	case WL_SHM_FORMAT_XRGB8888:
		fmt = CAIRO_FORMAT_RGB24;
		break;
	case WL_SHM_FORMAT_ARGB8888:
		fmt = CAIRO_FORMAT_ARGB32;
		break;
	default:
		log_warn("png: unexpected wl_shm format 0x%x; writing as RGB24", img->format);
		fmt = CAIRO_FORMAT_RGB24;
		break;
	}

	cairo_surface_t *surf = cairo_image_surface_create_for_data(
		img->bytes, fmt, img->width, img->height, img->stride);

	cairo_status_t st = cairo_surface_status(surf);
	if (st != CAIRO_STATUS_SUCCESS) {
		log_error("cairo_image_surface_create_for_data: %s", cairo_status_to_string(st));
		cairo_surface_destroy(surf);
		return -1;
	}

	st = cairo_surface_write_to_png(surf, path);
	cairo_surface_destroy(surf);

	if (st != CAIRO_STATUS_SUCCESS) {
		log_error("cairo_surface_write_to_png(%s): %s", path, cairo_status_to_string(st));
		return -1;
	}
	return 0;
}

int grabit_png_write_region(const struct image *img,
                           int32_t x, int32_t y, int32_t w, int32_t h,
                           const char *path) {
	if (!img || !img->bytes || !path) return -1;
	if (x < 0 || y < 0 || w <= 0 || h <= 0) return -1;
	if (x + w > img->width || y + h > img->height) return -1;

	cairo_format_t fmt;
	switch (img->format) {
	case WL_SHM_FORMAT_XRGB8888: fmt = CAIRO_FORMAT_RGB24;  break;
	case WL_SHM_FORMAT_ARGB8888: fmt = CAIRO_FORMAT_ARGB32; break;
	default:
		log_warn("png: unexpected wl_shm format 0x%x; writing as RGB24", img->format);
		fmt = CAIRO_FORMAT_RGB24;
		break;
	}

	cairo_surface_t *src = cairo_image_surface_create_for_data(
		img->bytes, fmt, img->width, img->height, img->stride);
	if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
		log_error("cairo source: %s", cairo_status_to_string(cairo_surface_status(src)));
		cairo_surface_destroy(src);
		return -1;
	}

	cairo_surface_t *dst = cairo_image_surface_create(fmt, w, h);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		log_error("cairo dest: %s", cairo_status_to_string(cairo_surface_status(dst)));
		cairo_surface_destroy(src);
		cairo_surface_destroy(dst);
		return -1;
	}

	cairo_t *cr = cairo_create(dst);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, src, -x, -y);
	cairo_paint(cr);
	cairo_destroy(cr);

	cairo_status_t st = cairo_surface_write_to_png(dst, path);
	cairo_surface_destroy(src);
	cairo_surface_destroy(dst);

	if (st != CAIRO_STATUS_SUCCESS) {
		log_error("cairo_surface_write_to_png(%s): %s", path, cairo_status_to_string(st));
		return -1;
	}
	return 0;
}

static cairo_format_t image_cairo_format(uint32_t fmt) {
	switch (fmt) {
	case WL_SHM_FORMAT_XRGB8888: return CAIRO_FORMAT_RGB24;
	case WL_SHM_FORMAT_ARGB8888: return CAIRO_FORMAT_ARGB32;
	default:                     return CAIRO_FORMAT_RGB24;
	}
}

int grabit_png_write_composite(int32_t dst_w, int32_t dst_h,
                               const struct png_slice *slices, size_t n,
                               const char *path) {
	if (!path || dst_w <= 0 || dst_h <= 0 || n == 0 || !slices) return -1;
	if (dst_w > GRABIT_MAX_PIXEL_SIDE || dst_h > GRABIT_MAX_PIXEL_SIDE) {
		log_error("png composite: %dx%d exceeds %d-px side cap",
		          dst_w, dst_h, GRABIT_MAX_PIXEL_SIDE);
		return -1;
	}

	cairo_surface_t *dst = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, dst_w, dst_h);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		log_error("cairo composite dst: %s",
		          cairo_status_to_string(cairo_surface_status(dst)));
		cairo_surface_destroy(dst);
		return -1;
	}

	cairo_t *cr = cairo_create(dst);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 1);
	cairo_paint(cr);

	for (size_t i = 0; i < n; i++) {
		const struct png_slice *s = &slices[i];
		if (!s->src || !s->src->bytes) continue;
		if (s->src_w <= 0 || s->src_h <= 0) continue;
		if (s->dst_w <= 0 || s->dst_h <= 0) continue;

		cairo_format_t fmt = image_cairo_format(s->src->format);
		cairo_surface_t *src = cairo_image_surface_create_for_data(
			s->src->bytes, fmt, s->src->width, s->src->height, s->src->stride);
		if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
			log_warn("composite slice %zu: %s",
			         i, cairo_status_to_string(cairo_surface_status(src)));
			cairo_surface_destroy(src);
			continue;
		}

		cairo_save(cr);
		cairo_rectangle(cr, s->dst_x, s->dst_y, s->dst_w, s->dst_h);
		cairo_clip(cr);
		double sx = (double)s->dst_w / (double)s->src_w;
		double sy = (double)s->dst_h / (double)s->src_h;
		cairo_translate(cr, s->dst_x, s->dst_y);
		cairo_scale(cr, sx, sy);
		cairo_translate(cr, -s->src_x, -s->src_y);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_surface_destroy(src);
	}

	cairo_destroy(cr);

	cairo_status_t st = cairo_surface_write_to_png(dst, path);
	cairo_surface_destroy(dst);

	if (st != CAIRO_STATUS_SUCCESS) {
		log_error("cairo_surface_write_to_png(%s): %s", path, cairo_status_to_string(st));
		return -1;
	}
	return 0;
}
