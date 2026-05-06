// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_state.h"

#include "capture/capture.h"
#include "region/annotate.h"
#include "region/wlr_input_state.h"
#include "wl.h"

#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

static const uint32_t TB_COLORS[6] = {
	0xff3030u,
	0xfff030u,
	0x40ff40u,
	0x4080ffu,
	0x000000u,
	0xffffffu,
};

static void apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
						 struct ro_output *o, struct wl_cursor *c);

static bool eyedropper_sample(struct ro_state *st, uint32_t *out_color) {
	if (!st->cursor_on || !st->frozen) return false;
	struct ro_output *ro = st->cursor_on;
	const struct image *img = &st->frozen[ro->idx];
	if (!img->bytes || img->stride <= 0) return false;
	int32_t scale = ro->go->scale > 0 ? ro->go->scale : 1;
	int32_t px = (st->cursor_x - ro->go->x) * scale;
	int32_t py = (st->cursor_y - ro->go->y) * scale;
	if (px < 0 || py < 0 || px >= img->width || py >= img->height) return false;
	const uint8_t *row = (const uint8_t *)img->bytes + (size_t)py * (size_t)img->stride;
	uint32_t pixel = ((const uint32_t *)row)[px];
	*out_color = pixel & 0xFFFFFFu;
	return true;
}

static struct wl_cursor *pick_cursor(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	if (st->region_locked) {
		if (st->moving_region && st->cursor_move) return st->cursor_move;
		if (st->handle_dragging >= 0 && st->handle_dragging < 8 &&
			st->cursor_resize[st->handle_dragging])
			return st->cursor_resize[st->handle_dragging];
		if (region_toolbar_contains(st, abs_x, abs_y)) {
			enum tb_action a = region_toolbar_hit(st, abs_x, abs_y);
			if (a != TB_NONE && st->cursor_hand) return st->cursor_hand;
			if (st->cursor_default) return st->cursor_default;
		}
		if (st->eyedropper_mode) return st->cursor;
		int h = region_handle_at(st, abs_x, abs_y);
		if (h != HANDLE_NONE && st->cursor_resize[h]) return st->cursor_resize[h];
		if (h != HANDLE_NONE && st->cursor_default) return st->cursor_default;
		if (st->ctrl_held && region_inside_selection(st, abs_x, abs_y) && st->cursor_move)
			return st->cursor_move;
		if (st->current_tool == TOOL_TEXT && st->cursor_text) return st->cursor_text;
		return st->cursor_default ? st->cursor_default : st->cursor;
	}
	return st->cursor;
}

static void refresh_cursor(struct ro_state *st, struct wl_pointer *p) {
	if (!st->cursor_on) return;
	struct wl_cursor *want = pick_cursor(st, st->cursor_x, st->cursor_y);
	if (want == st->current_cursor) return;
	st->current_cursor = want;
	if (st->last_cursor_serial == 0) return;
	apply_cursor(st, p, st->last_cursor_serial, st->cursor_on, want);
}

static void apply_cursor(struct ro_state *st, struct wl_pointer *p, uint32_t serial,
						 struct ro_output *o, struct wl_cursor *c) {
	if (!c || c->image_count == 0 || !st->cursor_surface) return;
	struct wl_cursor_image *img = c->images[0];
	struct wl_buffer *buf = wl_cursor_image_get_buffer(img);
	if (!buf) return;
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

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct ro_state *st = data;
	if (st->cleanup) return;
	struct ro_output *o = region_render_find_by_surface(st, surface);
	if (!o) return;
	st->cursor_on = o;
	st->cursor_x = o->go->x + wl_fixed_to_int(sx);
	st->cursor_y = o->go->y + wl_fixed_to_int(sy);
	st->last_cursor_serial = serial;
	apply_cursor(st, p, serial, o, pick_cursor(st, st->cursor_x, st->cursor_y));
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
						  struct wl_surface *surface) {
	(void)p;
	(void)serial;
	(void)surface;
	struct ro_state *st = data;
	if (st->cleanup) return;
	st->cursor_on = NULL;
	if (region_set_hover(st, -1)) region_render_request_redraw_all(st);
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time,
						   wl_fixed_t sx, wl_fixed_t sy) {
	(void)p;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!st->cursor_on) return;
	st->cursor_x = st->cursor_on->go->x + wl_fixed_to_int(sx);
	st->cursor_y = st->cursor_on->go->y + wl_fixed_to_int(sy);

	if (st->region_locked) {
		if (st->slider_dragging) {
			int32_t slx, sly, slw, slh;
			region_toolbar_slider_rect(st, &slx, &sly, &slw, &slh);
			(void)sly;
			(void)slh;
			double frac = slw > 0 ? (double)(st->cursor_x - slx) / (double)slw : 0;
			if (frac < 0) frac = 0;
			if (frac > 1) frac = 1;
			st->current_width = WIDTH_MIN +
								(int32_t)(frac * (WIDTH_MAX - WIDTH_MIN) + 0.5);
		} else if (st->color_picker_dragging) {
			uint32_t picked = 0;
			if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked)) {
				st->current_color = picked;
				st->edit_choices_dirty = true;
			}
		} else if (st->moving_region) {
			st->sel_x = st->cursor_x - st->move_grab_dx;
			st->sel_y = st->cursor_y - st->move_grab_dy;
		} else if (st->handle_dragging != HANDLE_NONE) {
			region_apply_handle_drag(st);
		} else if (st->drawing &&
				   (st->current_tool == TOOL_PEN || st->current_tool == TOOL_ERASER)) {
			region_pen_append(st, st->cursor_x, st->cursor_y);
		}
	} else {
		region_update_selection(st);
	}

	int hover = -1;
	if (st->region_locked && !st->drawing && !st->text_input_active &&
		!st->slider_dragging && !st->moving_region &&
		st->handle_dragging == HANDLE_NONE) {
		enum tb_action a = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
		if (a != TB_NONE) hover = (int)a;
	}
	region_set_hover(st, hover);

	refresh_cursor(st, p);
	region_render_request_redraw_all(st);
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial,
						   uint32_t time, uint32_t button, uint32_t state) {
	(void)p;
	(void)serial;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;

	if (button == BTN_RIGHT && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (region_drag_active(st) || st->text_input_active) {
			region_drag_abort(st);
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		st->cancelled = true;
		st->finished = true;
		return;
	}
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->slider_dragging) {
		st->slider_dragging = false;
		region_render_request_redraw_all(st);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_RELEASED && st->color_picker_dragging) {
		st->color_picker_dragging = false;
		region_render_request_redraw_all(st);
		return;
	}

	if (st->color_picker_open && state == WL_POINTER_BUTTON_STATE_PRESSED) {
		int32_t px, py, pw, ph;
		region_color_picker_rect(st, &px, &py, &pw, &ph);
		bool inside_grid = pw > 0 && ph > 0 &&
						   st->cursor_x >= px && st->cursor_x < px + pw &&
						   st->cursor_y >= py && st->cursor_y < py + ph;
		int32_t ix, iy, iw, ih;
		region_color_input_rect(st, &ix, &iy, &iw, &ih);
		bool inside_input = iw > 0 && ih > 0 &&
							st->cursor_x >= ix && st->cursor_x < ix + iw &&
							st->cursor_y >= iy && st->cursor_y < iy + ih;
		int32_t ex, ey, ew, eh;
		region_color_eyedropper_rect(st, &ex, &ey, &ew, &eh);
		bool inside_eyedropper = ew > 0 && eh > 0 &&
								 st->cursor_x >= ex && st->cursor_x < ex + ew &&
								 st->cursor_y >= ey && st->cursor_y < ey + eh;
		if (inside_eyedropper) {
			st->eyedropper_mode = !st->eyedropper_mode;
			st->color_input_active = false;
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_grid) {
			st->color_input_active = false;
			uint32_t picked = 0;
			if (region_color_picker_pick(st, st->cursor_x, st->cursor_y, &picked)) {
				st->current_color = picked;
				st->edit_choices_dirty = true;
			}
			st->color_picker_dragging = true;
			region_render_request_redraw_all(st);
			return;
		}
		if (inside_input) {
			st->color_input_active = true;
			snprintf(st->color_input_buf, sizeof st->color_input_buf,
					 "%06X", st->current_color & 0xFFFFFFu);
			st->color_input_len = 6;
			region_render_request_redraw_all(st);
			return;
		}
		if (st->color_input_active) {
			uint32_t parsed = 0;
			if (region_parse_hex_color(st->color_input_buf, &parsed)) {
				st->current_color = parsed;
				st->edit_choices_dirty = true;
			}
			st->color_input_active = false;
			st->color_input_len = 0;
		}
		if (st->eyedropper_mode) {
			/* fall through to eyedropper sample below; keep panel open */
		} else if (!region_toolbar_contains(st, st->cursor_x, st->cursor_y)) {
			st->color_picker_open = false;
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
	}

	if (st->region_locked && region_toolbar_contains(st, st->cursor_x, st->cursor_y)) {
		enum tb_action act = region_toolbar_hit(st, st->cursor_x, st->cursor_y);
		if (act != TB_NONE) {
			if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
				st->tooltip_visible = false;
				if (act != TB_WIDTH_SLIDER) region_tooltip_arm(st);
				if (st->text_input_active) region_commit_text(st);
				if (act >= TB_TOOL_PEN && act <= TB_TOOL_ERASER) {
					static const enum tool_kind ACT_TO_TOOL[] = {
						[TB_TOOL_PEN] = TOOL_PEN,
						[TB_TOOL_RECT] = TOOL_RECT,
						[TB_TOOL_ELLIPSE] = TOOL_ELLIPSE,
						[TB_TOOL_ARROW] = TOOL_ARROW,
						[TB_TOOL_BLUR] = TOOL_BLUR,
						[TB_TOOL_TEXT] = TOOL_TEXT,
						[TB_TOOL_ERASER] = TOOL_ERASER,
					};
					st->current_tool = ACT_TO_TOOL[act];
					refresh_cursor(st, p);
				} else if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE) {
					st->current_color = TB_COLORS[act - TB_COLOR_RED];
					st->edit_choices_dirty = true;
					st->eyedropper_mode = false;
					st->color_picker_open = false;
				} else if (act == TB_COLOR_CURRENT) {
					st->color_picker_open = !st->color_picker_open;
					st->eyedropper_mode = false;
					refresh_cursor(st, p);
				} else if (act == TB_WIDTH_SLIDER) {
					int32_t sx, sy, sw, sh;
					region_toolbar_slider_rect(st, &sx, &sy, &sw, &sh);
					(void)sy;
					(void)sh;
					double frac = sw > 0 ? (double)(st->cursor_x - sx) / (double)sw : 0;
					if (frac < 0) frac = 0;
					if (frac > 1) frac = 1;
					st->current_width = WIDTH_MIN +
										(int32_t)(frac * (WIDTH_MAX - WIDTH_MIN) + 0.5);
					st->slider_dragging = true;
					st->edit_choices_dirty = true;
					region_drag_start(st);
				} else if (act == TB_UNDO) {
					if (st->out_annos) annotation_list_pop(st->out_annos);
					region_undo_arm(st);
				} else if (act == TB_SAVE) {
					st->finished = true;
				} else if (act == TB_CANCEL) {
					st->cancelled = true;
					st->finished = true;
				}
			} else {
				if (act == TB_UNDO) region_undo_disarm(st);
			}
			region_render_request_redraw_all(st);
			return;
		}
		return;
	}

	if (!st->region_locked) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
			st->dragging = true;
			st->drag_x0 = st->cursor_x;
			st->drag_y0 = st->cursor_y;
			region_update_selection(st);
		} else {
			st->dragging = false;
			if (st->has_selection && (st->sel_w < 8 || st->sel_h < 8)) {
				st->has_selection = false;
				st->sel_w = st->sel_h = 0;
			}
			if (st->has_selection) {
				if (st->annotate_mode && st->out_annos) {
					st->region_locked = true;
				} else {
					st->finished = true;
				}
			}
		}
		region_render_request_redraw_all(st);
		return;
	}

	if (st->text_input_active) {
		if (state == WL_POINTER_BUTTON_STATE_PRESSED) region_commit_text(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		if (st->eyedropper_mode) {
			uint32_t picked = 0;
			if (eyedropper_sample(st, &picked)) {
				st->current_color = picked;
				st->edit_choices_dirty = true;
			}
			st->eyedropper_mode = false;
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		int h = region_handle_at(st, st->cursor_x, st->cursor_y);
		if (h != HANDLE_NONE) {
			st->handle_dragging = h;
			region_drag_start(st);
			region_render_request_redraw_all(st);
			return;
		}
		if (!region_inside_selection(st, st->cursor_x, st->cursor_y)) return;
		if (st->ctrl_held) {
			st->moving_region = true;
			st->move_grab_dx = st->cursor_x - st->sel_x;
			st->move_grab_dy = st->cursor_y - st->sel_y;
			region_drag_start(st);
			refresh_cursor(st, p);
			region_render_request_redraw_all(st);
			return;
		}
		if (st->current_tool == TOOL_TEXT) {
			st->text_input_active = true;
			st->text_x = st->cursor_x;
			st->text_y = st->cursor_y;
			st->text_len = 0;
			st->text_buf[0] = '\0';
			region_drag_start(st);
			region_render_request_redraw_all(st);
			return;
		}
		st->drawing = true;
		st->draw_x0 = st->cursor_x;
		st->draw_y0 = st->cursor_y;
		if (st->current_tool == TOOL_PEN || st->current_tool == TOOL_ERASER) {
			st->pen_n = 0;
			region_pen_append(st, st->draw_x0, st->draw_y0);
		}
		region_drag_start(st);
	} else {
		if (st->moving_region) {
			st->moving_region = false;
			refresh_cursor(st, p);
		} else if (st->handle_dragging != HANDLE_NONE) {
			st->handle_dragging = HANDLE_NONE;
			refresh_cursor(st, p);
		} else if (st->drawing) {
			region_commit_drawing(st);
		}
	}
	region_render_request_redraw_all(st);
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

static void handle_text_input(struct ro_state *st, xkb_keysym_t sym, uint32_t key) {
	if (sym == XKB_KEY_BackSpace) {
		if (st->text_len > 0) {
			st->text_len--;
			st->text_buf[st->text_len] = '\0';
		}
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		region_commit_text(st);
		return;
	}
	if (sym == XKB_KEY_Escape) {
		st->text_input_active = false;
		st->text_len = 0;
		return;
	}
	char buf[8];
	int n = xkb_state_key_get_utf8(st->xkb_state, key + 8, buf, sizeof buf);
	if (n <= 0) return;
	if ((unsigned char)buf[0] < 0x20) return;
	if (st->text_len + (size_t)n + 1 > sizeof st->text_buf) return;
	memcpy(st->text_buf + st->text_len, buf, (size_t)n);
	st->text_len += (size_t)n;
	st->text_buf[st->text_len] = '\0';
}

static void keyboard_key(void *data, struct wl_keyboard *kb, uint32_t serial,
						 uint32_t time, uint32_t key, uint32_t state) {
	(void)kb;
	(void)serial;
	(void)time;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (!st->xkb_state) return;
	xkb_keysym_t sym = xkb_state_key_get_one_sym(st->xkb_state, key + 8);
	if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (sym == XKB_KEY_u || sym == XKB_KEY_U) region_undo_disarm(st);
		return;
	}

	if (st->text_input_active) {
		handle_text_input(st, sym, key);
		region_render_request_redraw_all(st);
		return;
	}

	if (st->color_input_active) {
		if (sym == XKB_KEY_Escape) {
			st->color_input_active = false;
			st->color_input_len = 0;
		} else if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
			uint32_t parsed = 0;
			if (region_parse_hex_color(st->color_input_buf, &parsed)) {
				st->current_color = parsed;
				st->edit_choices_dirty = true;
			}
			st->color_input_active = false;
			st->color_input_len = 0;
		} else if (sym == XKB_KEY_BackSpace) {
			if (st->color_input_len > 0) {
				st->color_input_len--;
				st->color_input_buf[st->color_input_len] = '\0';
			}
		} else {
			char buf[8];
			int n = xkb_state_key_get_utf8(st->xkb_state, key + 8, buf, sizeof buf);
			if (n == 1) {
				char c = buf[0];
				bool is_hex = (c >= '0' && c <= '9') ||
							  (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
				if (is_hex && st->color_input_len < 6) {
					st->color_input_buf[st->color_input_len++] =
						(c >= 'a' && c <= 'f') ? (char)(c - 32) : c;
					st->color_input_buf[st->color_input_len] = '\0';
				}
			}
		}
		region_render_request_redraw_all(st);
		return;
	}

	if (sym == XKB_KEY_Escape) {
		if (region_drag_active(st)) {
			region_drag_abort(st);
			if (st->pointer) refresh_cursor(st, st->pointer);
			region_render_request_redraw_all(st);
			return;
		}
		st->cancelled = true;
		st->finished = true;
		return;
	}
	if (sym == XKB_KEY_Return || sym == XKB_KEY_KP_Enter) {
		if (st->region_locked) {
			st->finished = true;
		} else if (st->has_selection) {
			st->finished = true;
		} else {
			st->cancelled = true;
			st->finished = true;
		}
		return;
	}

	if (!st->region_locked) return;

	if (sym == XKB_KEY_u || sym == XKB_KEY_U) {
		if (region_drag_active(st)) return;
		if (st->out_annos) annotation_list_pop(st->out_annos);
		region_undo_arm(st);
		region_render_request_redraw_all(st);
		return;
	}

	if (region_drag_active(st)) return;

	int32_t pick = -1;
	switch (sym) {
	case XKB_KEY_1:
	case XKB_KEY_KP_1:
		pick = TOOL_PEN;
		break;
	case XKB_KEY_2:
	case XKB_KEY_KP_2:
		pick = TOOL_RECT;
		break;
	case XKB_KEY_3:
	case XKB_KEY_KP_3:
		pick = TOOL_ELLIPSE;
		break;
	case XKB_KEY_4:
	case XKB_KEY_KP_4:
		pick = TOOL_ARROW;
		break;
	case XKB_KEY_5:
	case XKB_KEY_KP_5:
		pick = TOOL_BLUR;
		break;
	case XKB_KEY_6:
	case XKB_KEY_KP_6:
		pick = TOOL_TEXT;
		break;
	case XKB_KEY_7:
	case XKB_KEY_KP_7:
		pick = TOOL_ERASER;
		break;
	default:
		break;
	}
	if (pick >= 0 && pick < TOOL_COUNT) {
		st->current_tool = (enum tool_kind)pick;
		if (st->pointer) refresh_cursor(st, st->pointer);
		region_render_request_redraw_all(st);
	}
}

static void keyboard_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
							   uint32_t mods_depressed, uint32_t mods_latched,
							   uint32_t mods_locked, uint32_t group) {
	(void)kb;
	(void)serial;
	struct ro_state *st = data;
	if (st->cleanup) return;
	if (st->xkb_state) {
		xkb_state_update_mask(st->xkb_state, mods_depressed, mods_latched,
							  mods_locked, 0, 0, group);
		st->shift_held = xkb_state_mod_name_is_active(
							 st->xkb_state, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0;
		st->ctrl_held = xkb_state_mod_name_is_active(
							st->xkb_state, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0;
		if (st->pointer) refresh_cursor(st, st->pointer);
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
