// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "capture/capture.h"
#include "log.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static int32_t i32max(int32_t a, int32_t b) {
	return a > b ? a : b;
}
static int32_t i32min(int32_t a, int32_t b) {
	return a < b ? a : b;
}

static int output_alloc_buffer(struct ro_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_width = o->width * o->scale;
	o->pixel_height = o->height * o->scale;

	if (o->pixel_width <= 0 || o->pixel_height <= 0 ||
		o->pixel_width > GRABIT_MAX_PIXEL_SIDE ||
		o->pixel_height > GRABIT_MAX_PIXEL_SIDE) {
		log_error("region: layer buffer %dx%d out of range",
				  o->pixel_width, o->pixel_height);
		return -1;
	}

	int stride = o->pixel_width * 4;
	size_t size = (size_t)stride * (size_t)o->pixel_height;
	int fd = grabit_shm_anon("grabit-region", size);
	if (fd < 0) return -1;

	o->buf_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (o->buf_data == MAP_FAILED) {
		log_error("mmap: %s", strerror(errno));
		close(fd);
		o->buf_data = NULL;
		return -1;
	}
	o->buf_size = size;
	o->stride = stride;

	struct wl_shm_pool *pool = wl_shm_create_pool(o->st->wls->shm, fd, (int32_t)size);
	o->buffer = wl_shm_pool_create_buffer(pool, 0, o->pixel_width, o->pixel_height, stride,
										  WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);

	wl_surface_set_buffer_scale(o->surface, o->scale);
	return 0;
}

void region_render_free_buffer(struct ro_output *o) {
	if (o->frame_cb) {
		wl_callback_destroy(o->frame_cb);
		o->frame_cb = NULL;
	}
	if (o->buffer) {
		wl_buffer_destroy(o->buffer);
		o->buffer = NULL;
	}
	if (o->buf_data) {
		munmap(o->buf_data, o->buf_size);
		o->buf_data = NULL;
		o->buf_size = 0;
	}
}

static void output_redraw(struct ro_output *o);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct ro_output *o = data;
	wl_callback_destroy(cb);
	o->frame_cb = NULL;
	if (o->dirty) output_redraw(o);
}

static const struct wl_callback_listener frame_listener_g = {
	.done = frame_done,
};

static void output_request_redraw(struct ro_output *o) {
	o->dirty = true;
	if (o->frame_cb) return;
	output_redraw(o);
}

static void output_redraw(struct ro_output *o) {
	if (!o->configured || !o->buf_data) return;
	o->dirty = false;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	cairo_surface_t *surf = cairo_image_surface_create_for_data(
		o->buf_data, CAIRO_FORMAT_ARGB32, pw, ph, o->stride);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		return;
	}
	cairo_t *cr = cairo_create(surf);

	const struct image *frozen = NULL;
	cairo_surface_t *fz = NULL;
	if (o->st->frozen) {
		const struct image *cand = &o->st->frozen[o->idx];
		if (cand->bytes && cand->width > 0 && cand->height > 0) {
			frozen = cand;
			cairo_format_t fmt = (frozen->format == WL_SHM_FORMAT_ARGB8888)
									 ? CAIRO_FORMAT_ARGB32
									 : CAIRO_FORMAT_RGB24;
			fz = cairo_image_surface_create_for_data(
				frozen->bytes, fmt, frozen->width, frozen->height, frozen->stride);
			if (cairo_surface_status(fz) != CAIRO_STATUS_SUCCESS) {
				cairo_surface_destroy(fz);
				fz = NULL;
				frozen = NULL;
			}
		}
	}

	cairo_pattern_t *fz_pat = NULL;
	if (fz) {
		fz_pat = cairo_pattern_create_for_surface(fz);
		double psx = frozen->width > 0 ? (double)pw / (double)frozen->width : 1.0;
		double psy = frozen->height > 0 ? (double)ph / (double)frozen->height : 1.0;
		cairo_matrix_t m;
		cairo_matrix_init_scale(&m, 1.0 / psx, 1.0 / psy);
		cairo_pattern_set_matrix(fz_pat, &m);
		cairo_pattern_set_filter(fz_pat, CAIRO_FILTER_GOOD);
	}

	if (fz_pat) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source(cr, fz_pat);
		cairo_paint(cr);
		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
		cairo_paint(cr);
	} else {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.45);
		cairo_paint(cr);
	}

	if (o->st->has_selection) {
		int32_t sx = (o->st->sel_x - o->go->x) * S;
		int32_t sy = (o->st->sel_y - o->go->y) * S;
		int32_t sw = o->st->sel_w * S;
		int32_t sh = o->st->sel_h * S;
		int32_t l = i32max(0, sx);
		int32_t t = i32max(0, sy);
		int32_t r = i32min(pw, sx + sw);
		int32_t b = i32min(ph, sy + sh);

		if (r > l && b > t) {
			cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
			if (fz_pat) {
				cairo_set_source(cr, fz_pat);
			} else {
				cairo_set_source_rgba(cr, 0, 0, 0, 0);
			}
			cairo_rectangle(cr, l, t, r - l, b - t);
			cairo_fill(cr);

			cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
			cairo_set_source_rgba(cr, 1, 1, 1, 0.9);
			cairo_set_line_width(cr, (double)S);
			double dashes[2] = {4.0 * S, 4.0 * S};
			cairo_set_dash(cr, dashes, 2, 0);
			double half = (double)S * 0.5;
			cairo_rectangle(cr, (double)l + half, (double)t + half,
							(double)(r - l) - (double)S, (double)(b - t) - (double)S);
			cairo_stroke(cr);
			cairo_set_dash(cr, NULL, 0, 0);

			char dims[32];
			snprintf(dims, sizeof dims, "%dx%d", o->st->sel_w, o->st->sel_h);
			cairo_select_font_face(cr, "sans-serif",
								   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
			cairo_set_font_size(cr, 14.0 * S);
			cairo_text_extents_t ext;
			cairo_text_extents(cr, dims, &ext);

			double tx = (double)r - ext.width - 8.0 * S;
			double ty = (double)b - 8.0 * S;
			cairo_set_source_rgba(cr, 0, 0, 0, 0.7);
			cairo_rectangle(cr, tx - 4.0 * S, ty - ext.height - 2.0 * S,
							ext.width + 8.0 * S, ext.height + 6.0 * S);
			cairo_fill(cr);
			cairo_set_source_rgba(cr, 1, 1, 1, 1);
			cairo_move_to(cr, tx, ty);
			cairo_show_text(cr, dims);
		}
	}

	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_surface_destroy(surf);
	if (fz_pat) cairo_pattern_destroy(fz_pat);
	if (fz) cairo_surface_destroy(fz);

	o->frame_cb = wl_surface_frame(o->surface);
	wl_callback_add_listener(o->frame_cb, &frame_listener_g, o);

	wl_surface_attach(o->surface, o->buffer, 0, 0);
	wl_surface_damage_buffer(o->surface, 0, 0, pw, ph);
	wl_surface_commit(o->surface);
}

void region_render_request_redraw_all(struct ro_state *st) {
	for (size_t i = 0; i < st->n_outs; i++)
		output_request_redraw(&st->outs[i]);
}

struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s) {
	for (size_t i = 0; i < st->n_outs; i++) {
		if (st->outs[i].surface == s) return &st->outs[i];
	}
	return NULL;
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct ro_output *o = data;
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	region_render_free_buffer(o);
	if (output_alloc_buffer(o) != 0) {
		o->st->cancelled = true;
		o->st->finished = true;
		return;
	}
	o->configured = true;
	output_redraw(o);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct ro_output *o = data;
	o->st->cancelled = true;
	o->st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

void region_render_attach_layer(struct ro_output *o) {
	zwlr_layer_surface_v1_add_listener(o->layer_surface,
									   &layer_surface_listener_g, o);
}
