// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "wl.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

static void update_selection(struct ro_state *st) {
	if (!st->dragging) {
		st->has_selection = false;
		return;
	}
	int32_t x0 = st->drag_x0, y0 = st->drag_y0;
	int32_t x1 = st->cursor_x, y1 = st->cursor_y;
	int32_t l = x0 < x1 ? x0 : x1;
	int32_t t = y0 < y1 ? y0 : y1;
	int32_t r = x0 > x1 ? x0 : x1;
	int32_t b = y0 > y1 ? y0 : y1;
	st->sel_x = l;
	st->sel_y = t;
	st->sel_w = r - l;
	st->sel_h = b - t;
	st->has_selection = (st->sel_w > 0 && st->sel_h > 0);
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_state *st = data;
	struct ro_output *o = region_render_find_by_surface(st, surface);
	if (!o) return;
	st->cursor_on = o;
	st->cursor_x = o->go->x + wl_fixed_to_int(sx);
	st->cursor_y = o->go->y + wl_fixed_to_int(sy);

	if (st->cursor && st->cursor_surface && st->cursor->image_count > 0) {
		struct wl_cursor_image *img = st->cursor->images[0];
		struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
		if (buf) {
			int32_t scale = o->scale > 0 ? o->scale : 1;
			int32_t hsx = (int32_t)img->hotspot_x / scale;
			int32_t hsy = (int32_t)img->hotspot_y / scale;
			wl_pointer_set_cursor(p, serial, st->cursor_surface, hsx, hsy);
			wl_surface_set_buffer_scale(st->cursor_surface, scale);
			wl_surface_attach(st->cursor_surface, buf, 0, 0);
			wl_surface_damage_buffer(st->cursor_surface, 0, 0,
									 (int32_t)img->width, (int32_t)img->height);
			wl_surface_commit(st->cursor_surface);
		}
	}
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	st->cursor_on = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (!st->cursor_on) return;
	st->cursor_x = st->cursor_on->go->x + wl_fixed_to_int(sx);
	st->cursor_y = st->cursor_on->go->y + wl_fixed_to_int(sy);
	update_selection(st);
	region_render_request_redraw_all(st);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	struct ro_state *st = data;

	if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		st->cancelled = true;
		st->finished = true;
		return;
	}
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		st->dragging = true;
		st->drag_x0 = st->cursor_x;
		st->drag_y0 = st->cursor_y;
		update_selection(st);
		region_render_request_redraw_all(st);
	} else {
		st->dragging = false;
		if (st->has_selection) {
			st->finished = true;
		} else {
			region_render_request_redraw_all(st);
		}
	}
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
						 uint32_t axis, wl_fixed_t value) {
	(void)data;
	(void)p;
	(void)time;
	(void)axis;
	(void)value;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter = pointer_enter,
	.leave = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis = pointer_axis,
};

static void keyboard_keymap(void *data, struct wl_keyboard *kb,
							uint32_t format, int32_t fd, uint32_t size) {
	(void)kb;
	struct ro_state *st = data;
	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
		close(fd);
		return;
	}
	void *map_str = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map_str == MAP_FAILED) return;

	if (st->xkb_state) xkb_state_unref(st->xkb_state);
	if (st->xkb_keymap) xkb_keymap_unref(st->xkb_keymap);

	size_t klen = size > 0 ? size - 1 : 0;
	st->xkb_keymap = xkb_keymap_new_from_buffer(
		st->xkb_ctx, map_str, klen, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	munmap(map_str, size);
	st->xkb_state = st->xkb_keymap ? xkb_state_new(st->xkb_keymap) : NULL;
}

static void keyboard_enter(void *data, struct wl_keyboard *kb, uint32_t serial,
						   struct wl_surface *surface, struct wl_array *keys) {
	(void)data;
	(void)kb;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
						   struct wl_surface *surface) {
	(void)data;
	(void)kb;
	(void)serial;
	(void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t time, uint32_t key, uint32_t state) {
	(void)kb;
	(void)serial;
	(void)time;
	struct ro_state *st = data;
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
	if (!st->xkb_state) return;

	xkb_keysym_t sym = xkb_state_key_get_one_sym(st->xkb_state, key + 8);
	if (sym == XKB_KEY_Escape) {
		st->cancelled = true;
		st->finished = true;
	} else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (st->has_selection) {
			st->finished = true;
		} else {
			st->cancelled = true;
			st->finished = true;
		}
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
							   uint32_t mods_depressed, uint32_t mods_latched,
							   uint32_t mods_locked, uint32_t group) {
	(void)kb;
	(void)serial;
	struct ro_state *st = data;
	if (st->xkb_state) {
		xkb_state_update_mask(st->xkb_state, mods_depressed, mods_latched,
							  mods_locked, 0, 0, group);
	}
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
								 int32_t rate, int32_t delay) {
	(void)data;
	(void)kb;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener_g = {
	.keymap = keyboard_keymap,
	.enter = keyboard_enter,
	.leave = keyboard_leave,
	.key = keyboard_key,
	.modifiers = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

void region_input_attach(struct ro_state *st) {
	wl_pointer_add_listener(st->pointer, &pointer_listener_g, st);
	wl_keyboard_add_listener(st->keyboard, &keyboard_listener_g, st);
}
