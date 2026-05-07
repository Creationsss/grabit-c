// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin_state.h"

#include "cursor.h"
#include "log.h"
#include "wl.h"

#include <stdlib.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

void pin_input_load_cursors(struct pin_state *st) {
	if (!st->wls->shm || !st->wls->compositor) return;
	st->cursor_theme = grabit_cursor_theme_load(st->wls->shm, st->scale);
	if (!st->cursor_theme) return;
	static const char *const hand[] = {
		"pointer",
		"hand2",
		"pointing_hand",
		"hand",
		"hand1",
		"left_ptr",
		NULL,
	};
	static const char *const move[] = {
		"grab",
		"openhand",
		"fleur",
		"move",
		"all-scroll",
		"left_ptr",
		NULL,
	};
	static const char *const grabbing[] = {
		"grabbing",
		"closedhand",
		"fleur",
		"move",
		"left_ptr",
		NULL,
	};
	st->cursor_hand = grabit_cursor_load_first(st->cursor_theme, hand);
	st->cursor_move = grabit_cursor_load_first(st->cursor_theme, move);
	st->cursor_grabbing = grabit_cursor_load_first(st->cursor_theme, grabbing);
	st->cursor_surface = wl_compositor_create_surface(st->wls->compositor);
}

void pin_input_destroy_cursors(struct pin_state *st) {
	if (st->cursor_surface) {
		wl_surface_destroy(st->cursor_surface);
		st->cursor_surface = NULL;
	}
	if (st->cursor_theme) {
		wl_cursor_theme_destroy(st->cursor_theme);
		st->cursor_theme = NULL;
	}
}

static void apply_cursor(struct pin_state *st, struct wl_cursor *c) {
	if (!st->pointer || !st->cursor_surface || st->last_pointer_serial == 0) return;
	if (!c || c->image_count == 0) return;
	struct wl_cursor_image *img = c->images[0];
	struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
	if (!buf) return;
	int32_t s = st->scale > 0 ? st->scale : 1;
	wl_pointer_set_cursor(st->pointer, st->last_pointer_serial, st->cursor_surface,
						  (int32_t)img->hotspot_x / s, (int32_t)img->hotspot_y / s);
	wl_surface_set_buffer_scale(st->cursor_surface, s);
	wl_surface_attach(st->cursor_surface, buf, 0, 0);
	wl_surface_damage_buffer(st->cursor_surface, 0, 0,
							 (int32_t)img->width, (int32_t)img->height);
	wl_surface_commit(st->cursor_surface);
}

static void update_cursor(struct pin_state *st);

void pin_input_apply_region(struct pin_state *st) {
	if (!st->surface || !st->wls->compositor) return;
	if (st->input_grabbed) {
		wl_surface_set_input_region(st->surface, NULL);
	} else {
		grabit_wl_clear_input_region(st->wls->compositor, st->surface);
	}
	wl_surface_commit(st->surface);
}

static bool in_close_button(const struct pin_state *st, int32_t sx, int32_t sy) {
	int32_t bx = st->width - PIN_CLOSE_BTN_SIZE - PIN_CLOSE_BTN_INSET;
	int32_t by = PIN_CLOSE_BTN_INSET;
	return sx >= bx && sx < bx + PIN_CLOSE_BTN_SIZE &&
		   sy >= by && sy < by + PIN_CLOSE_BTN_SIZE;
}

static void drag_frame_done(void *data, struct wl_callback *cb, uint32_t time);

static const struct wl_callback_listener drag_frame_listener_g = {
	.done = drag_frame_done,
};

static void flush_drag(struct pin_state *st) {
	int32_t dx_int = st->pending_dx_fixed / 256;
	int32_t dy_int = st->pending_dy_fixed / 256;
	if (dx_int == 0 && dy_int == 0) return;

	int32_t new_mx = st->margin_x + dx_int;
	int32_t new_my = st->margin_y + dy_int;
	if (new_mx < 0) {
		new_mx = 0;
		st->pending_dx_fixed = 0;
	} else {
		st->pending_dx_fixed -= dx_int * 256;
	}
	if (new_my < 0) {
		new_my = 0;
		st->pending_dy_fixed = 0;
	} else {
		st->pending_dy_fixed -= dy_int * 256;
	}
	if (new_mx == st->margin_x && new_my == st->margin_y) return;
	st->margin_x = new_mx;
	st->margin_y = new_my;
	zwlr_layer_surface_v1_set_margin(st->layer_surface,
									 st->margin_y, 0, 0, st->margin_x);
	st->drag_frame_cb = wl_surface_frame(st->surface);
	wl_callback_add_listener(st->drag_frame_cb, &drag_frame_listener_g, st);
	wl_surface_commit(st->surface);
}

static void drag_frame_done(void *data, struct wl_callback *cb, uint32_t time) {
	(void)time;
	struct pin_state *st = data;
	wl_callback_destroy(cb);
	st->drag_frame_cb = NULL;
	if (st->dragging) flush_drag(st);
}

static void apply_drag_delta(struct pin_state *st, wl_fixed_t dx, wl_fixed_t dy) {
	st->pending_dx_fixed += dx;
	st->pending_dy_fixed += dy;
	if (st->drag_frame_cb) return;
	flush_drag(st);
}

static void update_cursor(struct pin_state *st) {
	if (!st->pointer_in_surface) return;
	struct wl_cursor *want = NULL;
	if (st->input_grabbed) {
		if (st->dragging)
			want = st->cursor_grabbing;
		else if (in_close_button(st, st->cursor_sx, st->cursor_sy))
			want = st->cursor_hand;
		else
			want = st->cursor_move;
	}
	if (want == st->current_cursor) return;
	st->current_cursor = want;
	apply_cursor(st, want);
}

void pin_input_refresh_cursor(struct pin_state *st) {
	st->current_cursor = NULL;
	update_cursor(st);
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)surface;
	struct pin_state *st = data;
	st->cursor_sx = wl_fixed_to_int(sx);
	st->cursor_sy = wl_fixed_to_int(sy);
	st->last_pointer_serial = serial;
	st->pointer_in_surface = true;
	st->current_cursor = NULL;
	update_cursor(st);
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct pin_state *st = data;
	st->pointer_in_surface = false;
	st->dragging = false;
	st->pending_dx_fixed = 0;
	st->pending_dy_fixed = 0;
	st->current_cursor = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct pin_state *st = data;
	st->cursor_sx = wl_fixed_to_int(sx);
	st->cursor_sy = wl_fixed_to_int(sy);
	update_cursor(st);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)time;
	struct pin_state *st = data;
	st->last_pointer_serial = serial;
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (in_close_button(st, st->cursor_sx, st->cursor_sy)) {
			st->finished = true;
			return;
		}
		st->dragging = true;
		st->pending_dx_fixed = 0;
		st->pending_dy_fixed = 0;
	} else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
		st->dragging = false;
		st->pending_dx_fixed = 0;
		st->pending_dy_fixed = 0;
	}
	update_cursor(st);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}
static void pointer_frame(void *data, struct wl_pointer *p) {
	(void)data;
	(void)p;
}
static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source) {
	(void)data;
	(void)p;
	(void)source;
}
static void pointer_axis_stop(void *data, struct wl_pointer *p,
							  uint32_t time, uint32_t axis) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
}
static void pointer_axis_discrete(void *data, struct wl_pointer *p,
								  uint32_t axis, int32_t discrete) {
	(void)data;
	(void)p;
	(void)axis;
	(void)discrete;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
	.frame = pointer_frame,
	.axis_source = pointer_axis_source,
	.axis_stop = pointer_axis_stop,
	.axis_discrete = pointer_axis_discrete,
};

static void relative_motion(void *data, struct zwp_relative_pointer_v1 *rp,
							uint32_t utime_hi, uint32_t utime_lo,
							wl_fixed_t dx, wl_fixed_t dy,
							wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
	(void)rp;
	(void)utime_hi;
	(void)utime_lo;
	(void)dx_unaccel;
	(void)dy_unaccel;
	struct pin_state *st = data;
	if (!st->dragging) return;
	apply_drag_delta(st, dx, dy);
}

static const struct zwp_relative_pointer_v1_listener relative_listener_g = {
	.relative_motion = relative_motion,
};

void pin_input_attach(struct pin_state *st) {
	if (!st->wls->seat || !(st->wls->seat_caps & WL_SEAT_CAPABILITY_POINTER)) {
		log_warn("pin: no pointer on seat; click-to-close disabled (use kill <pid> to dismiss)");
		return;
	}
	st->pointer = wl_seat_get_pointer(st->wls->seat);
	if (!st->pointer) return;
	wl_pointer_add_listener(st->pointer, &pointer_listener_g, st);

	if (st->wls->relative_pointer_manager) {
		st->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
			st->wls->relative_pointer_manager, st->pointer);
		if (st->relative_pointer) {
			zwp_relative_pointer_v1_add_listener(st->relative_pointer,
												 &relative_listener_g, st);
		}
	} else {
		log_warn("pin: compositor lacks zwp_relative_pointer_manager_v1; drag disabled");
	}
}
