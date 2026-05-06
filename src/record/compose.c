// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "record/compose.h"

#include "capture/capture.h"
#include "log.h"
#include "region/region.h"
#include "wl.h"

#include <stdlib.h>
#include <string.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

int rec_layout_build(struct grabit_wl_state *s, struct rect r, struct rec_layout *out) {
	memset(out, 0, sizeof *out);
	if (!s || s->n_outputs == 0) return -1;

	int32_t max_scale = 1;
	size_t n_overlap = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;
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
	out->slice_caches = calloc(n_overlap, sizeof *out->slice_caches);
	if (!out->slice_caches) {
		free(out->slices);
		out->slices = NULL;
		return -1;
	}

	size_t k = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;

		struct rec_slice *sl = &out->slices[k++];
		sl->out = o;
		sl->src_x = ix - o->x;
		sl->src_y = iy - o->y;
		sl->src_w = iw;
		sl->src_h = ih;
		sl->dst_x = (ix - r.x) * max_scale;
		sl->dst_y = (iy - r.y) * max_scale;
		sl->dst_w = iw * max_scale;
		sl->dst_h = ih * max_scale;
	}
	out->n = k;
	out->dst_w = dst_w;
	out->dst_h = dst_h;
	out->dst_stride = stride;
	return 0;
}

bool rec_layout_is_direct(const struct rec_layout *layout) {
	if (!layout || layout->n != 1) return false;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->transform != WL_OUTPUT_TRANSFORM_NORMAL) return false;
	return sl->dst_x == 0 && sl->dst_y == 0 && sl->dst_w == layout->dst_w && sl->dst_h == layout->dst_h;
}

int rec_layout_capture_direct_into(struct grabit_wl_state *s, const struct rec_layout *layout,
								   bool cursor, void *dst, int32_t dst_stride, int32_t dst_h) {
	if (!s || !layout || !dst) return -1;
	if (layout->n != 1) return -1;
	const struct rec_slice *sl = &layout->slices[0];
	if (sl->out->dead) return -1;
	if (sl->src_w != layout->dst_w || sl->src_h != layout->dst_h) return -1;
	return capture_output_region_into(s, sl->out,
									  sl->src_x, sl->src_y, sl->src_w, sl->src_h,
									  cursor, dst, dst_stride, dst_h, NULL,
									  &layout->slice_caches[0]);
}

static int ensure_slice_scratch(struct rec_layout *layout, int32_t w, int32_t h) {
	if (layout->slice_scratch_w >= w && layout->slice_scratch_h >= h) return 0;
	int32_t new_w = layout->slice_scratch_w > w ? layout->slice_scratch_w : w;
	int32_t new_h = layout->slice_scratch_h > h ? layout->slice_scratch_h : h;
	size_t new_size = (size_t)new_w * 4 * (size_t)new_h;
	if (new_size > layout->slice_scratch_size) {
		void *p = realloc(layout->slice_scratch, new_size);
		if (!p) return -1;
		layout->slice_scratch = p;
		layout->slice_scratch_size = new_size;
	}
	layout->slice_scratch_w = new_w;
	layout->slice_scratch_h = new_h;
	return 0;
}

int rec_layout_capture_compose(struct grabit_wl_state *s, struct rec_layout *layout,
							   bool cursor, void *dst_buf) {
	if (!s || !layout || !dst_buf) return -1;

	bool fully_tiled = false;
	if (layout->n == 1) {
		const struct rec_slice *sl = &layout->slices[0];
		if (sl->dst_x == 0 && sl->dst_y == 0 &&
			sl->dst_w == layout->dst_w && sl->dst_h == layout->dst_h) {
			fully_tiled = true;
		}
	}
	if (!fully_tiled)
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

		if (ensure_slice_scratch(layout, sl->src_w, sl->src_h) != 0) continue;
		int32_t scratch_stride = sl->src_w * 4;
		uint32_t fmt_raw;
		if (capture_output_region_into(s, sl->out,
									   sl->src_x, sl->src_y, sl->src_w, sl->src_h,
									   cursor, layout->slice_scratch,
									   scratch_stride, sl->src_h, &fmt_raw,
									   &layout->slice_caches[i]) != 0) {
			continue;
		}
		captured++;

		cairo_format_t fmt = grabit_cairo_format_for_shm(fmt_raw);
		cairo_surface_t *src = cairo_image_surface_create_for_data(
			layout->slice_scratch, fmt, sl->src_w, sl->src_h, scratch_stride);
		if (cairo_surface_status(src) != CAIRO_STATUS_SUCCESS) {
			cairo_surface_destroy(src);
			continue;
		}

		int32_t visible_w = grabit_wl_transform_swaps(sl->out->transform) ? sl->src_h : sl->src_w;
		int32_t visible_h = grabit_wl_transform_swaps(sl->out->transform) ? sl->src_w : sl->src_h;
		bool needs_scale = visible_w != sl->dst_w || visible_h != sl->dst_h;
		double sx = visible_w > 0 ? (double)sl->dst_w / (double)visible_w : 1.0;
		double sy = visible_h > 0 ? (double)sl->dst_h / (double)visible_h : 1.0;

		cairo_save(cr);
		cairo_rectangle(cr, sl->dst_x, sl->dst_y, sl->dst_w, sl->dst_h);
		cairo_clip(cr);
		cairo_translate(cr, sl->dst_x, sl->dst_y);
		if (needs_scale) cairo_scale(cr, sx, sy);
		grabit_wl_transform_apply_inverse(cr, sl->out->transform, sl->src_w, sl->src_h);
		cairo_set_source_surface(cr, src, 0, 0);
		cairo_pattern_set_filter(cairo_get_source(cr),
								 needs_scale ? CAIRO_FILTER_GOOD : CAIRO_FILTER_NEAREST);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_paint(cr);
		cairo_restore(cr);

		cairo_surface_destroy(src);
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
	if (layout->slice_caches) {
		for (size_t i = 0; i < layout->n; i++)
			sc_pool_destroy(&layout->slice_caches[i]);
		free(layout->slice_caches);
	}
	free(layout->slices);
	free(layout->slice_scratch);
	memset(layout, 0, sizeof *layout);
}
