// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/overlay.h"

#include "log.h"
#include "region/region.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#define BORDER_LOGICAL 1

struct overlay_output {
	struct overlay_state *st;
	struct grabit_output *go;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_buffer *buffer;
	void *buf_data;
	size_t buf_size;
	int32_t width;
	int32_t height;
	int32_t pixel_width;
	int32_t pixel_height;
	int32_t scale;
	bool configured;
};

struct overlay_state {
	struct grabit_wl_state *wls;
	struct rect r;
	struct overlay_output *outs;
	size_t n;
};

static int alloc_buffer(struct overlay_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_width = o->width * o->scale;
	o->pixel_height = o->height * o->scale;

	struct grabit_shm_buf b;
	if (grabit_shm_argb_buf(o->st->wls->shm, "grabit-overlay",
							o->pixel_width, o->pixel_height, &b) != 0) {
		return -1;
	}
	o->buffer = b.buffer;
	o->buf_data = b.map;
	o->buf_size = b.size;

	wl_surface_set_buffer_scale(o->surface, o->scale);
	return 0;
}

static void draw_border(struct overlay_output *o) {
	cairo_surface_t *surf = cairo_image_surface_create_for_data(
		o->buf_data, CAIRO_FORMAT_ARGB32, o->pixel_width, o->pixel_height,
		o->pixel_width * 4);
	if (cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
		cairo_surface_destroy(surf);
		return;
	}
	cairo_t *cr = cairo_create(surf);

	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_rgba(cr, 0, 0, 0, 0);
	cairo_paint(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	const double S = (double)o->scale;
	double rx = (o->st->r.x - o->go->x) * S;
	double ry = (o->st->r.y - o->go->y) * S;
	double rw = o->st->r.w * S;
	double rh = o->st->r.h * S;
	double bw = BORDER_LOGICAL * S;

	cairo_set_source_rgba(cr, 1.0, 0.2, 0.2, 0.95);
	cairo_set_line_width(cr, bw);
	double dashes[2] = {4.0 * S, 4.0 * S};
	cairo_set_dash(cr, dashes, 2, 0);
	cairo_rectangle(cr, rx - bw / 2.0, ry - bw / 2.0, rw + bw, rh + bw);
	cairo_stroke(cr);
	cairo_set_dash(cr, NULL, 0, 0);

	char dims[32];
	snprintf(dims, sizeof dims, "%dx%d", o->st->r.w, o->st->r.h);
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0 * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, dims, &ext);

	double pad = 4.0 * S;
	double pillw = ext.width + 2 * pad;
	double pillh = ext.height + 2 * pad;
	double pillx = rx + rw - pillw;
	double pilly = ry - bw - pillh - 2.0 * S;

	cairo_set_source_rgba(cr, 0.85, 0.1, 0.1, 0.9);
	cairo_rectangle(cr, pillx, pilly, pillw, pillh);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, pillx + pad - ext.x_bearing,
				  pilly + pad - ext.y_bearing);
	cairo_show_text(cr, dims);

	cairo_destroy(cr);
	cairo_surface_flush(surf);
	cairo_surface_destroy(surf);
}

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
									uint32_t serial, uint32_t w, uint32_t h) {
	struct overlay_output *o = data;
	o->width = (int32_t)w;
	o->height = (int32_t)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	struct grabit_shm_buf cfgbuf = {
		.buffer = o->buffer,
		.map = o->buf_data,
		.size = o->buf_size,
	};
	grabit_shm_buf_destroy(&cfgbuf);
	o->buffer = NULL;
	o->buf_data = NULL;
	o->buf_size = 0;

	if (alloc_buffer(o) != 0) return;
	draw_border(o);

	wl_surface_attach(o->surface, o->buffer, 0, 0);
	wl_surface_damage_buffer(o->surface, 0, 0, o->pixel_width, o->pixel_height);
	wl_surface_commit(o->surface);
	o->configured = true;
}

static void layer_surface_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
	(void)data;
	(void)ls;
}

static const struct zwlr_layer_surface_v1_listener layer_listener_g = {
	.configure = layer_surface_configure,
	.closed = layer_surface_closed,
};

static bool output_overlaps(const struct grabit_output *o, const struct rect *r) {
	int32_t lx = r->x > o->x ? r->x : o->x;
	int32_t ly = r->y > o->y ? r->y : o->y;
	int32_t rx = (r->x + r->w) < (o->x + o->logical_width) ? (r->x + r->w) : (o->x + o->logical_width);
	int32_t ry = (r->y + r->h) < (o->y + o->logical_height) ? (r->y + r->h) : (o->y + o->logical_height);
	return rx > lx && ry > ly;
}

struct overlay_state *overlay_start(struct grabit_wl_state *s, struct rect r) {
	if (!s || !s->layer_shell || !s->compositor) return NULL;

	size_t n_overlap = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (output_overlaps(s->outputs[i], &r)) n_overlap++;
	}
	if (n_overlap == 0) return NULL;

	struct overlay_state *st = calloc(1, sizeof *st);
	if (!st) return NULL;
	st->wls = s;
	st->r = r;
	st->outs = calloc(n_overlap, sizeof *st->outs);
	if (!st->outs) {
		free(st);
		return NULL;
	}
	st->n = n_overlap;

	size_t k = 0;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (!output_overlaps(s->outputs[i], &r)) continue;
		struct overlay_output *o = &st->outs[k++];
		o->st = st;
		o->go = s->outputs[i];

		o->surface = wl_compositor_create_surface(s->compositor);
		o->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			s->layer_shell, o->surface, o->go->wl_output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
			"grabit-overlay");
		zwlr_layer_surface_v1_add_listener(o->layer_surface, &layer_listener_g, o);

		zwlr_layer_surface_v1_set_anchor(o->layer_surface,
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
											 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
											 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
											 ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(o->layer_surface, 0, 0);
		zwlr_layer_surface_v1_set_exclusive_zone(o->layer_surface, -1);
		zwlr_layer_surface_v1_set_keyboard_interactivity(
			o->layer_surface,
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

		struct wl_region *empty = wl_compositor_create_region(s->compositor);
		wl_surface_set_input_region(o->surface, empty);
		wl_region_destroy(empty);

		wl_surface_commit(o->surface);
	}

	wl_display_roundtrip(s->display);

	return st;
}

void overlay_stop(struct overlay_state *st) {
	if (!st) return;
	for (size_t i = 0; i < st->n; i++) {
		struct overlay_output *o = &st->outs[i];
		struct grabit_shm_buf b = {
			.buffer = o->buffer,
			.map = o->buf_data,
			.size = o->buf_size,
		};
		grabit_shm_buf_destroy(&b);
		if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(st->outs);
	if (st->wls && st->wls->display) wl_display_roundtrip(st->wls->display);
	free(st);
}
