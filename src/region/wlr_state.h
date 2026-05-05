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

#include "region/region.h"

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
	struct wl_cursor *cursor_text;
	struct wl_cursor *cursor_default;
	struct wl_cursor *cursor_move;
	struct wl_cursor *cursor_hand;
	struct wl_cursor *cursor_resize[8];
	struct wl_cursor *current_cursor;
	struct wl_surface *cursor_surface;
	uint32_t last_cursor_serial;

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
	bool cleanup;

	const struct image *frozen;

	bool annotate_mode;
	bool region_locked;
	enum tool_kind current_tool;

	bool drawing;
	int32_t draw_x0;
	int32_t draw_y0;
	int32_t *pen_points;
	size_t pen_n;
	size_t pen_cap;

	bool text_input_active;
	char text_buf[256];
	size_t text_len;
	int32_t text_x;
	int32_t text_y;

	struct annotation_list *out_annos;

	uint32_t current_color;
	int32_t current_width;
	bool edit_choices_dirty;
	bool shift_held;
	bool ctrl_held;
	int handle_dragging;
	bool moving_region;
	int32_t move_grab_dx;
	int32_t move_grab_dy;
	bool slider_dragging;

	int undo_timer_fd;
	bool undo_held;

	int tooltip_timer_fd;
	int hovered_button;
	bool tooltip_visible;
};

enum tb_action {
	TB_NONE = -1,
	TB_TOOL_PEN = 0,
	TB_TOOL_RECT,
	TB_TOOL_ELLIPSE,
	TB_TOOL_ARROW,
	TB_TOOL_BLUR,
	TB_TOOL_TEXT,
	TB_TOOL_ERASER,
	TB_COLOR_RED,
	TB_COLOR_YELLOW,
	TB_COLOR_GREEN,
	TB_COLOR_BLUE,
	TB_COLOR_BLACK,
	TB_COLOR_WHITE,
	TB_WIDTH_SLIDER,
	TB_UNDO,
	TB_SAVE,
	TB_CANCEL,
	TB_BTN_COUNT,
};

#define TB_BTN_W 38
#define TB_BTN_H 38
#define TB_PAD 6
#define TB_GAP 12

void region_toolbar_rect(const struct ro_state *st,
						 const struct grabit_output **out_o,
						 int32_t *x, int32_t *y, int32_t *w, int32_t *h);
enum tb_action region_toolbar_hit(const struct ro_state *st,
								  int32_t abs_x, int32_t abs_y);
bool region_toolbar_contains(const struct ro_state *st, int32_t abs_x, int32_t abs_y);
void region_toolbar_slider_rect(const struct ro_state *st,
								int32_t *out_x, int32_t *out_y,
								int32_t *out_w, int32_t *out_h);

#define WIDTH_MIN 1
#define WIDTH_MAX 12
void region_toolbar_render(cairo_t *cr, const struct ro_output *o);
void region_toolbar_tooltip_render(cairo_t *cr, const struct ro_output *o);

void region_render_attach_layer(struct ro_output *o);
void region_render_free_buffer(struct ro_output *o);
void region_render_request_redraw_all(struct ro_state *st);
struct ro_output *region_render_find_by_surface(struct ro_state *st, struct wl_surface *s);

void region_input_attach(struct ro_state *st);

#endif
