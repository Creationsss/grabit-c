// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "util.h"
#include "wl.h"

#include <math.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

int pin_render_alloc_buffer(struct pin_state *st) {
	st->pixel_width = st->width * st->scale;
	st->pixel_height = st->height * st->scale;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(st->wls->shm, "grabit-pin",
							st->pixel_width, st->pixel_height, &b) != 0) {
		return -1;
	}
	st->buffer = b.buffer;
	st->buf_data = b.map;
	st->buf_size = b.size;
	st->stride = st->pixel_width * 4;

	wl_surface_set_buffer_scale(st->surface, st->scale);
	return 0;
}

void pin_render_free_buffer(struct pin_state *st) {
	grabit_shm_release(&st->buffer, &st->buf_data, &st->buf_size);
}

static void draw_close_button(cairo_t *cr, int32_t width) {
	double bw = PIN_CLOSE_BTN_SIZE;
	double bx = (double)width - bw - PIN_CLOSE_BTN_INSET;
	double by = PIN_CLOSE_BTN_INSET;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_set_source_rgba(cr, 0, 0, 0, 0.78);
	cairo_arc(cr, bx + bw / 2.0, by + bw / 2.0, bw / 2.0, 0, 2.0 * M_PI);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_set_line_width(cr, 2.5);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	double pad = 7.5;
	cairo_move_to(cr, bx + pad, by + pad);
	cairo_line_to(cr, bx + bw - pad, by + bw - pad);
	cairo_move_to(cr, bx + bw - pad, by + pad);
	cairo_line_to(cr, bx + pad, by + bw - pad);
	cairo_stroke(cr);

	cairo_restore(cr);
}

void pin_render_paint(struct pin_state *st) {
	if (!st->configured || !st->buf_data || !st->image) return;

	cairo_surface_t *dst = cairo_image_surface_create_for_data(
		st->buf_data, CAIRO_FORMAT_ARGB32,
		st->pixel_width, st->pixel_height, st->stride);
	if (cairo_surface_status(dst) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(dst);
		return;
	}
	cairo_t *cr = cairo_create(dst);

	cairo_save(cr);
	double sx = st->img_w > 0 ? (double)st->pixel_width / (double)st->img_w : 1.0;
	double sy = st->img_h > 0 ? (double)st->pixel_height / (double)st->img_h : 1.0;
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_scale(cr, sx, sy);
	cairo_set_source_surface(cr, st->image, 0, 0);
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
	cairo_paint(cr);
	cairo_restore(cr);

	if (st->input_grabbed && st->width > 0) {
		cairo_save(cr);
		double dpr = (double)st->pixel_width / (double)st->width;
		cairo_scale(cr, dpr, dpr);
		draw_close_button(cr, st->width);
		cairo_restore(cr);
	}

	cairo_destroy(cr);
	cairo_surface_flush(dst);
	cairo_surface_destroy(dst);

	wl_surface_attach(st->surface, st->buffer, 0, 0);
	wl_surface_damage_buffer(st->surface, 0, 0, st->pixel_width, st->pixel_height);
	wl_surface_commit(st->surface);
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct pin_state *st = data;
	if (w > 0) st->width = (int32_t)w;
	if (h > 0) st->height = (int32_t)h;
	if (st->width <= 0) st->width = st->img_w;
	if (st->height <= 0) st->height = st->img_h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	pin_render_free_buffer(st);
	if (pin_render_alloc_buffer(st) != 0) {
		st->finished = true;
		return;
	}
	st->configured = true;
	pin_input_apply_region(st);
	pin_render_paint(st);
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)ls;
	struct pin_state *st = data;
	st->finished = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

void pin_render_attach_layer(struct pin_state *st) {
	zwlr_layer_surface_v1_add_listener(st->layer_surface,
									   &layer_surface_listener_g, st);
}
