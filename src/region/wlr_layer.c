// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/region.h"

#include "log.h"
#include "region/annotate.h"
#include "region/wlr_state.h"
#include "wl.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "wlr-layer-shell-unstable-v1-client-protocol.h"

int region_select(struct grabit_wl_state *s, const struct image *frozen,
				  bool annotate_mode, struct rect *out,
				  struct annotation_list *out_annos,
				  uint32_t *inout_color, int32_t *inout_width,
				  bool *out_choices_dirty) {
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
	st.annotate_mode = annotate_mode;
	st.out_annos = out_annos;
	st.current_tool = TOOL_PEN;
	st.current_color = (inout_color && *inout_color) ? *inout_color : 0xff3030u;
	st.current_width = (inout_width && *inout_width) ? *inout_width : 4;
	st.handle_dragging = -1;
	st.hovered_button = -1;
	st.outs = calloc(s->n_outputs, sizeof *st.outs);
	if (!st.outs) return -1;
	st.n_outs = s->n_outputs;

	st.xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (!st.xkb_ctx) {
		log_error("xkb_context_new failed");
		free(st.outs);
		return -1;
	}

	st.undo_timer_fd = annotate_mode
						   ? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)
						   : -1;
	st.tooltip_timer_fd = annotate_mode
							  ? timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK)
							  : -1;

	st.pointer = wl_seat_get_pointer(s->seat);
	st.keyboard = wl_seat_get_keyboard(s->seat);
	region_input_attach(&st);

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->scale > max_scale) max_scale = s->outputs[i]->scale;
	}
	const char *theme_name = getenv("XCURSOR_THEME");
	int32_t theme_size = 24;
	const char *size_env = getenv("XCURSOR_SIZE");
	if (size_env && *size_env) {
		char *end = NULL;
		long v = strtol(size_env, &end, 10);
		if (end != size_env && v >= 8 && v <= 256) theme_size = (int32_t)v;
	}
	st.cursor_theme = wl_cursor_theme_load(theme_name, theme_size * max_scale, s->shm);
	if (!st.cursor_theme) {
		log_warn("region: no cursor theme found; cursor may be invisible");
	} else {
		const char *cross_names[] = {
			"crosshair",
			"tcross",
			"cross",
			"cell",
			"left_ptr",
			"default",
			"arrow",
			NULL,
		};
		for (size_t i = 0; cross_names[i] && !st.cursor; i++) {
			st.cursor = wl_cursor_theme_get_cursor(st.cursor_theme, cross_names[i]);
		}
		const char *text_names[] = {
			"text",
			"xterm",
			"ibeam",
			"left_ptr",
			NULL,
		};
		for (size_t i = 0; text_names[i] && !st.cursor_text; i++) {
			st.cursor_text = wl_cursor_theme_get_cursor(st.cursor_theme, text_names[i]);
		}
		const char *default_names[] = {
			"left_ptr",
			"default",
			"arrow",
			NULL,
		};
		for (size_t i = 0; default_names[i] && !st.cursor_default; i++) {
			st.cursor_default = wl_cursor_theme_get_cursor(st.cursor_theme, default_names[i]);
		}
		const char *move_names[] = {
			"fleur",
			"move",
			"grabbing",
			"grab",
			"all-scroll",
			"left_ptr",
			NULL,
		};
		for (size_t i = 0; move_names[i] && !st.cursor_move; i++) {
			st.cursor_move = wl_cursor_theme_get_cursor(st.cursor_theme, move_names[i]);
		}
		const char *hand_names[] = {
			"pointer",
			"hand2",
			"pointing_hand",
			"hand",
			"hand1",
			"left_ptr",
			NULL,
		};
		for (size_t i = 0; hand_names[i] && !st.cursor_hand; i++) {
			st.cursor_hand = wl_cursor_theme_get_cursor(st.cursor_theme, hand_names[i]);
		}
		static const char *resize_names[8][6] = {
			{"nw-resize", "top_left_corner", "size_fdiag", NULL, NULL, NULL},
			{"n-resize", "top_side", "size_ver", NULL, NULL, NULL},
			{"ne-resize", "top_right_corner", "size_bdiag", NULL, NULL, NULL},
			{"e-resize", "right_side", "size_hor", NULL, NULL, NULL},
			{"se-resize", "bottom_right_corner", "size_fdiag", NULL, NULL, NULL},
			{"s-resize", "bottom_side", "size_ver", NULL, NULL, NULL},
			{"sw-resize", "bottom_left_corner", "size_bdiag", NULL, NULL, NULL},
			{"w-resize", "left_side", "size_hor", NULL, NULL, NULL},
		};
		for (size_t i = 0; i < 8; i++) {
			for (size_t j = 0; resize_names[i][j] && !st.cursor_resize[i]; j++) {
				st.cursor_resize[i] = wl_cursor_theme_get_cursor(
					st.cursor_theme, resize_names[i][j]);
			}
		}
		if (!st.cursor) log_warn("region: cursor theme has no usable cursor");
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
		while (wl_display_prepare_read(s->display) != 0) {
			if (wl_display_dispatch_pending(s->display) < 0) {
				st.cancelled = true;
				goto loop_done;
			}
		}
		if (st.finished) {
			wl_display_cancel_read(s->display);
			break;
		}
		if (wl_display_flush(s->display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(s->display);
			st.cancelled = true;
			break;
		}

		struct pollfd pfds[3];
		pfds[0].fd = wl_display_get_fd(s->display);
		pfds[0].events = POLLIN;
		int nfds = 1;
		int undo_idx = -1, tip_idx = -1;
		if (st.undo_timer_fd >= 0) {
			pfds[nfds].fd = st.undo_timer_fd;
			pfds[nfds].events = POLLIN;
			undo_idx = nfds++;
		}
		if (st.tooltip_timer_fd >= 0) {
			pfds[nfds].fd = st.tooltip_timer_fd;
			pfds[nfds].events = POLLIN;
			tip_idx = nfds++;
		}

		if (poll(pfds, (nfds_t)nfds, -1) < 0) {
			wl_display_cancel_read(s->display);
			if (errno == EINTR) continue;
			st.cancelled = true;
			break;
		}

		if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			wl_display_cancel_read(s->display);
			log_error("region: lost wayland connection");
			st.cancelled = true;
			break;
		}

		if (pfds[0].revents & POLLIN) {
			if (wl_display_read_events(s->display) < 0) {
				st.cancelled = true;
				break;
			}
		} else {
			wl_display_cancel_read(s->display);
		}
		if (wl_display_dispatch_pending(s->display) < 0) {
			st.cancelled = true;
			break;
		}

		if (undo_idx >= 0 && (pfds[undo_idx].revents & POLLIN)) {
			uint64_t expirations = 0;
			ssize_t r = read(st.undo_timer_fd, &expirations, sizeof expirations);
			(void)r;
			if (st.undo_held) {
				if (st.out_annos) annotation_list_pop(st.out_annos);
				region_render_request_redraw_all(&st);
			}
		}
		if (tip_idx >= 0 && (pfds[tip_idx].revents & POLLIN)) {
			uint64_t expirations = 0;
			ssize_t r = read(st.tooltip_timer_fd, &expirations, sizeof expirations);
			(void)r;
			if (st.hovered_button >= 0 && !st.tooltip_visible) {
				st.tooltip_visible = true;
				region_render_request_redraw_all(&st);
			}
		}
	}
loop_done:;

	int rc = -1;
	if (!st.cancelled && st.has_selection) {
		out->x = st.sel_x;
		out->y = st.sel_y;
		out->w = st.sel_w;
		out->h = st.sel_h;
		rc = 0;
	}
	if (inout_color) *inout_color = st.current_color;
	if (inout_width) *inout_width = st.current_width;
	if (out_choices_dirty) *out_choices_dirty = st.edit_choices_dirty;

	st.cleanup = true;
	if (st.pointer) wl_pointer_release(st.pointer);
	if (st.keyboard) wl_keyboard_release(st.keyboard);
	st.pointer = NULL;
	st.keyboard = NULL;
	wl_display_roundtrip(s->display);

	for (size_t i = 0; i < st.n_outs; i++) {
		struct ro_output *o = &st.outs[i];
		if (o->frame_cb) {
			wl_callback_destroy(o->frame_cb);
			o->frame_cb = NULL;
		}
		if (o->surface && o->buffer) {
			wl_surface_attach(o->surface, NULL, 0, 0);
			wl_surface_commit(o->surface);
		}
	}
	wl_display_roundtrip(s->display);

	for (size_t i = 0; i < st.n_outs; i++) {
		struct ro_output *o = &st.outs[i];
		region_render_free_buffer(o);
		if (o->layer_surface) zwlr_layer_surface_v1_destroy(o->layer_surface);
		if (o->surface) wl_surface_destroy(o->surface);
	}
	free(st.outs);

	if (st.cursor_surface) wl_surface_destroy(st.cursor_surface);
	if (st.cursor_theme) wl_cursor_theme_destroy(st.cursor_theme);

	if (st.xkb_state) xkb_state_unref(st.xkb_state);
	if (st.xkb_keymap) xkb_keymap_unref(st.xkb_keymap);
	if (st.xkb_ctx) xkb_context_unref(st.xkb_ctx);

	free(st.pen_points);
	if (st.undo_timer_fd >= 0) close(st.undo_timer_fd);
	if (st.tooltip_timer_fd >= 0) close(st.tooltip_timer_fd);

	return rc;
}
