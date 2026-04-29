// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "record/compose.h"

#include "capture/capture.h"
#include "log.h"
#include "region/region.h"
#include "wl.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int rec_layout_build(struct grabit_wl_state *s, struct rect r, struct rec_layout *out) {
	memset(out, 0, sizeof *out);
	if (!s || s->n_outputs == 0) return -1;

	int32_t max_scale = 1;
	size_t n_overlap = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t lx = r.x > o->x ? r.x : o->x;
		int32_t ly = r.y > o->y ? r.y : o->y;
		int32_t rx = (r.x + r.w) < (o->x + o->logical_width) ? (r.x + r.w) : (o->x + o->logical_width);
		int32_t ry = (r.y + r.h) < (o->y + o->logical_height) ? (r.y + r.h) : (o->y + o->logical_height);
		if (rx <= lx || ry <= ly) continue;
		n_overlap++;
		if (o->scale > max_scale) max_scale = o->scale;
	}
	if (n_overlap == 0) return -1;

	int32_t dst_w = r.w * max_scale;
	int32_t dst_h = r.h * max_scale;
	if (dst_w & 1) dst_w--;
	if (dst_h & 1) dst_h--;
	if (dst_w <= 0 || dst_h <= 0) return -1;

	int32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, dst_w);
	if (stride <= 0) return -1;

	out->slices = calloc(n_overlap, sizeof *out->slices);
	if (!out->slices) return -1;

	size_t k = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t lx = r.x > o->x ? r.x : o->x;
		int32_t ly = r.y > o->y ? r.y : o->y;
		int32_t rx = (r.x + r.w) < (o->x + o->logical_width) ? (r.x + r.w) : (o->x + o->logical_width);
		int32_t ry = (r.y + r.h) < (o->y + o->logical_height) ? (r.y + r.h) : (o->y + o->logical_height);
		int32_t iw = rx - lx, ih = ry - ly;
		if (iw <= 0 || ih <= 0) continue;

		struct rec_slice *sl = &out->slices[k++];
		sl->out = o;
		sl->src_x = lx - o->x;
		sl->src_y = ly - o->y;
		sl->src_w = iw;
		sl->src_h = ih;
		sl->dst_x = (lx - r.x) * max_scale;
		sl->dst_y = (ly - r.y) * max_scale;
		sl->dst_w = iw * max_scale;
		sl->dst_h = ih * max_scale;
	}
	out->n = k;
	out->dst_w = dst_w;
	out->dst_h = dst_h;
	out->dst_stride = stride;
	return 0;
}

static cairo_format_t image_cairo_format(uint32_t fmt) {
	if (fmt == WL_SHM_FORMAT_ARGB8888) return CAIRO_FORMAT_ARGB32;
	return CAIRO_FORMAT_RGB24;
}

bool rec_layout_is_direct(const struct rec_layout *layout) {
	if (!layout || layout->n != 1) return false;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->transform != WL_OUTPUT_TRANSFORM_NORMAL) return false;
	return sl->dst_x == 0 && sl->dst_y == 0 && sl->dst_w == layout->dst_w && sl->dst_h == layout->dst_h;
}

static bool transform_swaps_dims(int32_t t) {
	return t == WL_OUTPUT_TRANSFORM_90 ||
		   t == WL_OUTPUT_TRANSFORM_270 ||
		   t == WL_OUTPUT_TRANSFORM_FLIPPED_90 ||
		   t == WL_OUTPUT_TRANSFORM_FLIPPED_270;
}

static void apply_inverse_transform(cairo_t *cr, int32_t transform,
									int32_t src_w, int32_t src_h) {
	switch (transform) {
	case WL_OUTPUT_TRANSFORM_NORMAL:
		break;
	case WL_OUTPUT_TRANSFORM_90:
		cairo_translate(cr, 0, src_w);
		cairo_rotate(cr, -M_PI / 2);
		break;
	case WL_OUTPUT_TRANSFORM_180:
		cairo_translate(cr, src_w, src_h);
		cairo_rotate(cr, M_PI);
		break;
	case WL_OUTPUT_TRANSFORM_270:
		cairo_translate(cr, src_h, 0);
		cairo_rotate(cr, M_PI / 2);
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

int rec_layout_capture_direct(struct grabit_wl_state *s, const struct rec_layout *layout,
							  bool cursor, void **out_buf, int32_t *out_stride) {
	if (!s || !layout || !out_buf || !out_stride) return -1;
	if (layout->n != 1) return -1;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->dead) return -1;
	struct image img = {0};
	if (capture_output_region(s, sl->out,
							  sl->src_x, sl->src_y, sl->src_w, sl->src_h,
							  cursor, &img) != 0) {
		return -1;
	}
	if (img.width != layout->dst_w || img.height != layout->dst_h) {
		image_free(&img);
		return -1;
	}
	*out_buf = img.bytes;
	*out_stride = img.stride;
	return 0;
}

int rec_layout_capture_compose(struct grabit_wl_state *s, const struct rec_layout *layout,
							   bool cursor, void *dst_buf) {
	if (!s || !layout || !dst_buf) return -1;

	memset(dst_buf, 0, (size_t)layout->dst_stride * (size_t)layout->dst_h);

	cairo_surface_t *dst = cairo_image_surface_create_for_data(
		dst_buf, CAIRO_FORMAT_ARGB32, layout->dst_w, layout->dst_h, layout->dst_stride);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		log_error("compose: dst surface: %s",
				  cairo_status_to_string(cairo_surface_status(dst)));
		cairo_surface_destroy(dst);
		return -1;
	}
	cairo_t *cr = cairo_create(dst);

	size_t alive = 0, captured = 0;
	for (size_t i = 0; i < layout->n; i++) {
		const struct rec_slice *sl = &layout->slices[i];
		if (sl->out->dead) continue;
		alive++;

		struct image img = {0};
		if (capture_output_region(s, sl->out,
								  sl->src_x, sl->src_y, sl->src_w, sl->src_h,
								  cursor, &img) != 0) {
			continue;
		}
		captured++;

		cairo_format_t fmt = image_cairo_format(img.format);
		cairo_surface_t *src = cairo_image_surface_create_for_data(
			img.bytes, fmt, img.width, img.height, img.stride);
		if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(src);
			image_free(&img);
			continue;
		}

		int32_t visible_w = transform_swaps_dims(sl->out->transform) ? img.height : img.width;
		int32_t visible_h = transform_swaps_dims(sl->out->transform) ? img.width : img.height;
		bool needs_scale = visible_w != sl->dst_w || visible_h != sl->dst_h;
		double sx = visible_w > 0 ? (double)sl->dst_w / (double)visible_w : 1.0;
		double sy = visible_h > 0 ? (double)sl->dst_h / (double)visible_h : 1.0;

		cairo_save(cr);
		cairo_rectangle(cr, sl->dst_x, sl->dst_y, sl->dst_w, sl->dst_h);
		cairo_clip(cr);
		cairo_translate(cr, sl->dst_x, sl->dst_y);
		if (needs_scale) cairo_scale(cr, sx, sy);
		apply_inverse_transform(cr, sl->out->transform, img.width, img.height);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr),
								 needs_scale ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_surface_destroy(src);
		image_free(&img);
	}

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);
	if (alive == 0) return -1;
	if (captured == 0) return -1;
	return 0;
}

void rec_layout_free(struct rec_layout *layout) {
	if (!layout) return;
	free(layout->slices);
	memset(layout, 0, sizeof *layout);
}
