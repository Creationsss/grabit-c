// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAIRO_UTIL_H
#define GRABIT_CAIRO_UTIL_H

#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

static inline void grabit_cairo_set_source_argb(cairo_t *cr, uint32_t color, double alpha) {
	double r = ((color >> 16) & 0xff) / 255.0;
	double g = ((color >> 8) & 0xff) / 255.0;
	double b = (color & 0xff) / 255.0;
	cairo_set_source_rgba(cr, r, g, b, alpha);
}

static inline cairo_surface_t *grabit_cairo_image(void *data, cairo_format_t fmt,
												  int32_t w, int32_t h, int32_t stride) {
	cairo_surface_t *s = cairo_image_surface_create_for_data(data, fmt, w, h, stride);
	if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(s);
		return NULL;
	}
	return s;
}

static inline cairo_surface_t *grabit_cairo_image_argb(void *data, int32_t w, int32_t h,
													   int32_t stride) {
	return grabit_cairo_image(data, CAIRO_FORMAT_ARGB32, w, h, stride);
}

#endif
