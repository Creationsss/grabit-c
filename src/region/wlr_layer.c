// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "region/region.h"

#include "capture/capture.h"
#include "log.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <cairo/cairo.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct ro_state;

struct ro_output {
	struct ro_state                  *st;
	struct grabit_output             *go;
	size_t                            idx;

	struct wl_surface                *surface;
	struct zwlr_layer_surface_v1     *layer_surface;

	int32_t  width;          // logical
	int32_t  height;         // logical
	int32_t  pixel_width;    // logical * scale
	int32_t  pixel_height;
	int32_t  scale;
	bool     configured;

	int      stride;
	size_t   buf_size;
	void    *buf_data;
	struct wl_buffer                 *buffer;

	bool                              dirty;
	struct wl_callback               *frame_cb;
};

struct ro_state {
	struct grabit_wl_state                  *wls;
	struct ro_output                 *outs;
	size_t                            n_outs;

	struct wl_pointer                *pointer;
	struct wl_keyboard               *keyboard;

	struct wl_cursor_theme           *cursor_theme;
	struct wl_cursor                 *cursor;
	struct wl_surface                *cursor_surface;

	struct xkb_context               *xkb_ctx;
	struct xkb_keymap                *xkb_keymap;
	struct xkb_state                 *xkb_state;

	struct ro_output                 *cursor_on;
	int32_t                           cursor_x;
	int32_t                           cursor_y;

	bool                              dragging;
	int32_t                           drag_x0;
	int32_t                           drag_y0;

	bool                              has_selection;
	int32_t                           sel_x;
	int32_t                           sel_y;
	int32_t                           sel_w;
	int32_t                           sel_h;

	bool                              finished;
	bool                              cancelled;

	const struct image               *frozen;
};

static int output_alloc_buffer(struct ro_output *o) {
	o->scale = o->go->scale > 0 ? o->go->scale : 1;
	o->pixel_width  = o->width  * o->scale;
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

static void output_free_buffer(struct ro_output *o) {
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

static int32_t i32max(int32_t a, int32_t b) { return a > b ? a : b; }
static int32_t i32min(int32_t a, int32_t b) { return a < b ? a : b; }

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
				? CAIRO_FORMAT_ARGB32 : CAIRO_FORMAT_RGB24;
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
		double psx = frozen->width  > 0 ? (double)pw / (double)frozen->width  : 1.0;
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
			double dashes[2] = { 4.0 * S, 4.0 * S };
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

static void request_redraw_all(struct ro_state *st) {
	for (size_t i = 0; i < st->n_outs; i++) output_request_redraw(&st->outs[i]);
}

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

static void layer_surface_configure(void *data, struct zwlr_layer_surface_v1 *ls,
                                    uint32_t serial, uint32_t w, uint32_t h) {
	struct ro_output *o = data;
	o->width  = (int32_t)w;
	o->height = (int32_t)h;
	zwlr_layer_surface_v1_ack_configure(ls, serial);

	output_free_buffer(o);
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
	o->st->finished  = true;
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener_g = {
	.configure = layer_surface_configure,
	.closed    = layer_surface_closed,
};

static struct ro_output *find_by_surface(struct ro_state *st, struct wl_surface *s) {
	for (size_t i = 0; i < st->n_outs; i++) {
		if (st->outs[i].surface == s) return &st->outs[i];
	}
	return NULL;
}

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_state *st = data;
	struct ro_output *o = find_by_surface(st, surface);
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
	(void)p; (void)serial; (void)surface;
	struct ro_state *st = data;
	st->cursor_on = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
                           wl_fixed_t sx, wl_fixed_t sy) {
	(void)p; (void)time;
	struct ro_state *st = data;
	if (!st->cursor_on) return;
	st->cursor_x = st->cursor_on->go->x + wl_fixed_to_int(sx);
	st->cursor_y = st->cursor_on->go->y + wl_fixed_to_int(sy);
	update_selection(st);
	request_redraw_all(st);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {
	(void)p; (void)serial; (void)time;
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
		request_redraw_all(st);
	} else {
		st->dragging = false;
		if (st->has_selection) {
			st->finished = true;
		} else {
			request_redraw_all(st);
		}
	}
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {
	(void)data; (void)p; (void)time; (void)axis; (void)value;
}

static const struct wl_pointer_listener pointer_listener_g = {
	.enter  = pointer_enter,
	.leave  = pointer_leave,
	.motion = pointer_motion,
	.button = pointer_button,
	.axis   = pointer_axis,
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
	(void)data; (void)kb; (void)serial; (void)surface; (void)keys;
}

static void keyboard_leave(void *data, struct wl_keyboard *kb, uint32_t serial,
                           struct wl_surface *surface) {
	(void)data; (void)kb; (void)serial; (void)surface;
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t time, uint32_t key, uint32_t state) {
	(void)kb; (void)serial; (void)time;
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
	(void)kb; (void)serial;
	struct ro_state *st = data;
	if (st->xkb_state) {
		xkb_state_update_mask(st->xkb_state, mods_depressed, mods_latched,
		                      mods_locked, 0, 0, group);
	}
}

static void keyboard_repeat_info(void *data, struct wl_keyboard *kb,
                                 int32_t rate, int32_t delay) {
	(void)data; (void)kb; (void)rate; (void)delay;
}

static const struct wl_keyboard_listener keyboard_listener_g = {
	.keymap      = keyboard_keymap,
	.enter       = keyboard_enter,
	.leave       = keyboard_leave,
	.key         = keyboard_key,
	.modifiers   = keyboard_modifiers,
	.repeat_info = keyboard_repeat_info,
};

int region_select(struct grabit_wl_state *s, const struct image *frozen, struct rect *out) {
	if (!s->layer_shell) {
		log_error("region: compositor lacks zwlr_layer_shell_v1");
		return -1;
	}
	if (!s->compositor) {
		log_error("region: compositor lacks wl_compositor (impossible?)");
		return -1;
	}
	if (!(s->seat_caps & WL_SEAT_CAPABILITY_POINTER) ||
	    !(s->seat_caps & WL_SEAT_CAPABILITY_KEYBOARD)) {
		log_error("region: seat needs both pointer and keyboard");
		return -1;
	}

	struct ro_state st = { .wls = s, .frozen = frozen };
	st.outs = calloc(s->n_outputs, sizeof *st.outs);
	if (!st.outs) return -1;
	st.n_outs = s->n_outputs;

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) {
		log_error("xkb_context_new failed");
		free(st.outs);
		return -1;
	}

	st.pointer  = wl_seat_get_pointer(s->seat);
	st.keyboard = wl_seat_get_keyboard(s->seat);
	wl_pointer_add_listener(st.pointer, &pointer_listener_g, &st);
	wl_keyboard_add_listener(st.keyboard, &keyboard_listener_g, &st);

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
	}
	st.cursor_theme = wl_cursor_theme_load(NULL, 24 * max_scale, s->shm);
	if (!st.cursor_theme) {
		log_warn("region: no cursor theme found — cursor may be invisible");
	} else {
		const char *names[] = {
			"crosshair", "tcross", "cross", "cell",
			"left_ptr", "default", "arrow",
			NULL,
		};
		for (size_t i = 0; names[i] && !st.cursor; i++) {
			st.cursor = wl_cursor_theme_get_cursor(st.cursor_theme, names[i]);
		}
		if (!st.cursor) {
			log_warn("region: cursor theme has no usable cursor");
		}
	}
	if (st.cursor) {
		st.cursor_surface = wl_compositor_create_surface(s->compositor);
	}

	for (size_t i = 0; i < st.n_outs; i++) {
		struct ro_output *o = &st.outs[i];
		o->st = &st;
		o->go = s->outputs[i];
		o->idx = i;

		o->surface = wl_compositor_create_surface(s->compositor);
		o->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			s->layer_shell, o->surface, o->go->wl_output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
			"grabit-region");
		zwlr_layer_surface_v1_add_listener(o->layer_surface,
		                                   &layer_surface_listener_g, o);

		zwlr_layer_surface_v1_set_anchor(o->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP    |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT   |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
		zwlr_layer_surface_v1_set_size(o->layer_surface, 0, 0);
		zwlr_layer_surface_v1_set_exclusive_zone(o->layer_surface, -1);
		zwlr_layer_surface_v1_set_keyboard_interactivity(
			o->layer_surface,
			ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_EXCLUSIVE);

		wl_surface_commit(o->surface);
	}

	while (!st.finished) {
		if (wl_display_dispatch(s->display) < 0) {
			log_error("region: lost wayland connection");
			st.cancelled = true;
			break;
		}
	}

	int rc = -1;
	if (!st.cancelled && st.has_selection) {
		out->x = st.sel_x;
		out->y = st.sel_y;
		out->w = st.sel_w;
		out->h = st.sel_h;
		rc = 0;
	}

	for (size_t i = 0; i < st.n_outs; i++) {
		struct ro_output *o = &st.outs[i];
		output_free_buffer(o);
		if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(st.outs);

	if (st.cursor_surface) wl_surface_destroy(st.cursor_surface);
	if (st.cursor_theme)   wl_cursor_theme_destroy(st.cursor_theme);

	if (st.pointer)    wl_pointer_release(st.pointer);
	if (st.keyboard)   wl_keyboard_release(st.keyboard);
	if (st.xkb_state)  xkb_state_unref(st.xkb_state);
	if (st.xkb_keymap) xkb_keymap_unref(st.xkb_keymap);
	if (st.xkb_ctx)    xkb_context_unref(st.xkb_ctx);

	wl_display_roundtrip(s->display);
	return rc;
}
