// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_STATE_H
#define GRABIT_PIN_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

struct grabit_wl_state;
struct zwlr_layer_surface_v1;
struct zwp_relative_pointer_v1;
struct wl_cursor;
struct wl_cursor_theme;

struct pin_state {
	struct grabit_wl_state *wls;

	cairo_surface_t *image;
	int32_t img_w;
	int32_t img_h;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;
	struct wl_pointer *pointer;
	struct zwp_relative_pointer_v1 *relative_pointer;

	int32_t width;
	int32_t height;
	int32_t scale;
	int32_t pixel_width;
	int32_t pixel_height;
	int stride;
	size_t buf_size;
	void *buf_data;
	struct wl_buffer *buffer;
	cairo_surface_t *dst_surface;
	bool configured;

	bool input_grabbed;
	bool finished;

	int32_t margin_x;
	int32_t margin_y;

	int32_t cursor_sx;
	int32_t cursor_sy;
	bool dragging;
	bool pointer_in_surface;
	int32_t pending_dx_fixed;
	int32_t pending_dy_fixed;
	struct wl_callback *drag_frame_cb;

	struct wl_cursor_theme *cursor_theme;
	struct wl_surface *cursor_surface;
	struct wl_cursor *cursor_hand;
	struct wl_cursor *cursor_move;
	struct wl_cursor *cursor_grabbing;
	struct wl_cursor *current_cursor;
	uint32_t last_pointer_serial;

	int ipc_fd;
	char ipc_path[256];
};

#define PIN_CLOSE_BTN_SIZE 24
#define PIN_CLOSE_BTN_INSET 4

int pin_render_alloc_buffer(struct pin_state *st);
void pin_render_free_buffer(struct pin_state *st);
void pin_render_paint(struct pin_state *st);
void pin_render_repaint_button_area(struct pin_state *st);
void pin_render_attach_layer(struct pin_state *st);

void pin_input_attach(struct pin_state *st);
void pin_input_apply_region(struct pin_state *st);
void pin_input_load_cursors(struct pin_state *st);
void pin_input_destroy_cursors(struct pin_state *st);
void pin_input_refresh_cursor(struct pin_state *st);

int pin_ipc_open(struct pin_state *st);
void pin_ipc_close(struct pin_state *st);
void pin_ipc_handle(struct pin_state *st);

int pin_ipc_broadcast(const char *msg);

#endif
