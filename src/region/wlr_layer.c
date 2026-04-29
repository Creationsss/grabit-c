// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/region.h"

#include "log.h"
#include "region/wlr_state.h"
#include "wl.h"

#include <stdlib.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

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

	struct ro_state st = {.wls = s, .frozen = frozen};
	st.outs = calloc(s->n_outputs, sizeof *st.outs);
	if (!st.outs) return -1;
	st.n_outs = s->n_outputs;

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) {
		log_error("xkb_context_new failed");
		free(st.outs);
		return -1;
	}

	st.pointer = wl_seat_get_pointer(s->seat);
	st.keyboard = wl_seat_get_keyboard(s->seat);
	region_input_attach(&st);

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
	}
	st.cursor_theme = wl_cursor_theme_load(NULL, 24 * max_scale, s->shm);
	if (!st.cursor_theme) {
		log_warn("region: no cursor theme found; cursor may be invisible");
	} else {
		const char *names[] = {
			"crosshair",
			"tcross",
			"cross",
			"cell",
			"left_ptr",
			"default",
			"arrow",
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
		region_render_attach_layer(o);

		zwlr_layer_surface_v1_set_anchor(o->layer_surface,
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
											 ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
											 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
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
		region_render_free_buffer(o);
		if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(st.outs);

	if (st.cursor_surface) wl_surface_destroy(st.cursor_surface);
	if (st.cursor_theme) wl_cursor_theme_destroy(st.cursor_theme);

	if (st.pointer) wl_pointer_release(st.pointer);
	if (st.keyboard) wl_keyboard_release(st.keyboard);
	if (st.xkb_state) xkb_state_unref(st.xkb_state);
	if (st.xkb_keymap) xkb_keymap_unref(st.xkb_keymap);
	if (st.xkb_ctx) xkb_context_unref(st.xkb_ctx);

	wl_display_roundtrip(s->display);
	return rc;
}
