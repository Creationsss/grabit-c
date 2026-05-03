// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_WLR_STATE_H
#define GRABIT_REGION_WLR_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

struct grabit_wl_state;
struct grabit_output;
struct image;
struct wl_cursor_theme;
struct wl_cursor;
struct zwlr_layer_surface_v1;

struct ro_state;

struct ro_output {
	struct ro_state *st;
	struct grabit_output *go;
	size_t idx;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	int32_t width;
	int32_t height;
	int32_t pixel_width;
	int32_t pixel_height;
	int32_t scale;
	bool configured;

	int stride;
	size_t buf_size;
	void *buf_data;
	struct wl_buffer *buffer;

	cairo_surface_t *cairo_dst;
	cairo_surface_t *cairo_frozen;
	cairo_pattern_t *cairo_frozen_pat;

	bool dirty;
	struct wl_callback *frame_cb;
};

struct ro_state {
	struct grabit_wl_state *wls;
	struct ro_output *outs;
	size_t n_outs;

	struct wl_pointer *pointer;
	struct wl_keyboard *keyboard;

	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor *cursor;
	struct wl_surface *cursor_surface;

	struct xkb_context *xkb_ctx;
	struct xkb_keymap *xkb_keymap;
	struct xkb_state *xkb_state;

	struct ro_output *cursor_on;
	int32_t cursor_x;
	int32_t cursor_y;

	bool dragging;
	int32_t drag_x0;
	int32_t drag_y0;

	bool has_selection;
	int32_t sel_x;
	int32_t sel_y;
	int32_t sel_w;
	int32_t sel_h;

	bool finished;
	bool cancelled;

	const struct image *frozen;
};

void region_render_attach_layer(struct ro_output *o);
void region_render_free_buffer(struct ro_output *o);
void region_render_request_redraw_all(struct ro_state *st);
struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s);

void region_input_attach(struct ro_state *st);

#endif
