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

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-region",
							o->pixel_width, o->pixel_height, &b) != 0) {
		return -1;
	}
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;
	o->stride = o->pixel_width * 4;

	o->cairo_dst = cairo_image_surface_create_for_data(
		o->buf_data, CAIRO_FORMAT_ARGB32, o->pixel_width, o->pixel_height, o->stride);
	if (cairo_surface_status(o->cairo_dst) != CAIRO_STATUS_SUCCESS) {
		log_error("region: cairo dst surface: %s",
				  cairo_status_to_string(cairo_surface_status(o->cairo_dst)));
		cairo_surface_destroy(o->cairo_dst);
		o->cairo_dst = NULL;
		return -1;
	}

	const struct image *frozen = NULL;
	if (o->st->frozen) {
		const struct image *cand = &o->st->frozen[o->idx];
		if (cand->bytes && cand->width > 0 && cand->height > 0) frozen = cand;
	}
	if (frozen) {
		cairo_format_t fmt = (frozen->format == WL_SHM_FORMAT_ARGB8888)
								 ? CAIRO_FORMAT_ARGB32
								 : CAIRO_FORMAT_RGB24;
		o->cairo_frozen = cairo_image_surface_create_for_data(
			frozen->bytes, fmt, frozen->width, frozen->height, frozen->stride);
		if (cairo_surface_status(o->cairo_frozen) == CAIRO_STATUS_SUCCESS) {
			o->cairo_frozen_pat = cairo_pattern_create_for_surface(o->cairo_frozen);
			double psx = frozen->width > 0
							 ? (double)o->pixel_width / (double)frozen->width
							 : 1.0;
			double psy = frozen->height > 0
							 ? (double)o->pixel_height / (double)frozen->height
							 : 1.0;
			cairo_matrix_t m;
			cairo_matrix_init_scale(&m, 1.0 / psx, 1.0 / psy);
			cairo_pattern_set_matrix(o->cairo_frozen_pat, &m);
			cairo_pattern_set_filter(o->cairo_frozen_pat, CAIRO_FILTER_GOOD);
		} else {
			cairo_surface_destroy(o->cairo_frozen);
			o->cairo_frozen = NULL;
		}
	}

	wl_surface_set_buffer_scale(o->surface, o->scale);
	return 0;
}

void region_render_free_buffer(struct ro_output *o) {
	if (o->frame_cb) {
		wl_callback_destroy(o->frame_cb);
		o->frame_cb = NULL;
	}
	if (o->cairo_frozen_pat) {
		cairo_pattern_destroy(o->cairo_frozen_pat);
		o->cairo_frozen_pat = NULL;
	}
	if (o->cairo_frozen) {
		cairo_surface_destroy(o->cairo_frozen);
		o->cairo_frozen = NULL;
	}
	if (o->cairo_dst) {
		cairo_surface_destroy(o->cairo_dst);
		o->cairo_dst = NULL;
	}
	struct grabit_shm_buf b = {
		.buffer = o->buffer,
		.map = o->buf_data,
		.size = o->buf_size,
	};
	grabit_shm_buf_destroy(&b);
	o->buffer = NULL;
	o->buf_data = NULL;
	o->buf_size = 0;
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
	if (!o->configured || !o->buf_data || !o->cairo_dst) return;
	o->dirty = false;

	const int32_t S = o->scale;
	const int32_t pw = o->pixel_width;
	const int32_t ph = o->pixel_height;

	cairo_t *cr = cairo_create(o->cairo_dst);

	if (o->cairo_frozen_pat) {
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source(cr, o->cairo_frozen_pat);
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
			if (o->cairo_frozen_pat) {
				cairo_set_source(cr, o->cairo_frozen_pat);
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
	cairo_surface_flush(o->cairo_dst);

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
