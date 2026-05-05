// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/wlr_input_state.h"

#include "region/annotate.h"
#include "region/wlr_state.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/timerfd.h>

#define ANNO_DEFAULT_FONT 18
#define UNDO_HOLD_DELAY_MS 600
#define UNDO_HOLD_REPEAT_MS 80
#define TOOLTIP_DELAY_MS 1000
#define PEN_POINTS_MAX (1u << 18)

int region_handle_at(const struct ro_state *st, int32_t x, int32_t y) {
	if (!st->region_locked) return HANDLE_NONE;
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t mx = (l + r) / 2, my = (t + b) / 2;
	struct {
		int kind;
		int32_t hx, hy;
	} h[] = {
		{HANDLE_NW, l, t},
		{HANDLE_N, mx, t},
		{HANDLE_NE, r, t},
		{HANDLE_E, r, my},
		{HANDLE_SE, r, b},
		{HANDLE_S, mx, b},
		{HANDLE_SW, l, b},
		{HANDLE_W, l, my},
	};
	for (size_t i = 0; i < sizeof h / sizeof h[0]; i++) {
		int32_t dx = x - h[i].hx, dy = y - h[i].hy;
		if (dx * dx + dy * dy <= HANDLE_RADIUS * HANDLE_RADIUS) return h[i].kind;
	}
	return HANDLE_NONE;
}

static int flip_handle_x(int h) {
	switch (h) {
	case HANDLE_NW: return HANDLE_NE;
	case HANDLE_NE: return HANDLE_NW;
	case HANDLE_E: return HANDLE_W;
	case HANDLE_SE: return HANDLE_SW;
	case HANDLE_SW: return HANDLE_SE;
	case HANDLE_W: return HANDLE_E;
	default: return h;
	}
}

static int flip_handle_y(int h) {
	switch (h) {
	case HANDLE_NW: return HANDLE_SW;
	case HANDLE_N: return HANDLE_S;
	case HANDLE_NE: return HANDLE_SE;
	case HANDLE_SE: return HANDLE_NE;
	case HANDLE_S: return HANDLE_N;
	case HANDLE_SW: return HANDLE_NW;
	default: return h;
	}
}

void region_apply_handle_drag(struct ro_state *st) {
	int32_t l = st->sel_x, r = st->sel_x + st->sel_w;
	int32_t t = st->sel_y, b = st->sel_y + st->sel_h;
	int32_t cx = st->cursor_x, cy = st->cursor_y;
	switch (st->handle_dragging) {
	case HANDLE_NW:
		l = cx;
		t = cy;
		break;
	case HANDLE_N:
		t = cy;
		break;
	case HANDLE_NE:
		r = cx;
		t = cy;
		break;
	case HANDLE_E:
		r = cx;
		break;
	case HANDLE_SE:
		r = cx;
		b = cy;
		break;
	case HANDLE_S:
		b = cy;
		break;
	case HANDLE_SW:
		l = cx;
		b = cy;
		break;
	case HANDLE_W:
		l = cx;
		break;
	default:
		return;
	}
	if (l > r) {
		int32_t tmp = l;
		l = r;
		r = tmp;
		st->handle_dragging = flip_handle_x(st->handle_dragging);
	}
	if (t > b) {
		int32_t tmp = t;
		t = b;
		b = tmp;
		st->handle_dragging = flip_handle_y(st->handle_dragging);
	}
	st->sel_x = l;
	st->sel_y = t;
	st->sel_w = r - l;
	st->sel_h = b - t;
}

void region_undo_arm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = true;
	struct itimerspec it = {
		.it_value = {.tv_sec = UNDO_HOLD_DELAY_MS / 1000,
					 .tv_nsec = (UNDO_HOLD_DELAY_MS % 1000) * 1000000L},
		.it_interval = {.tv_sec = UNDO_HOLD_REPEAT_MS / 1000,
						.tv_nsec = (UNDO_HOLD_REPEAT_MS % 1000) * 1000000L},
	};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

void region_undo_disarm(struct ro_state *st) {
	if (st->undo_timer_fd < 0) return;
	st->undo_held = false;
	struct itimerspec it = {0};
	timerfd_settime(st->undo_timer_fd, 0, &it, NULL);
}

void region_tooltip_arm(struct ro_state *st) {
	if (st->tooltip_timer_fd < 0) return;
	struct itimerspec it = {
		.it_value = {.tv_sec = TOOLTIP_DELAY_MS / 1000,
					 .tv_nsec = (TOOLTIP_DELAY_MS % 1000) * 1000000L},
	};
	timerfd_settime(st->tooltip_timer_fd, 0, &it, NULL);
}

void region_tooltip_disarm(struct ro_state *st) {
	if (st->tooltip_timer_fd < 0) return;
	struct itimerspec it = {0};
	timerfd_settime(st->tooltip_timer_fd, 0, &it, NULL);
}

void region_drag_start(struct ro_state *st) {
	st->hovered_button = -1;
	st->tooltip_visible = false;
	region_tooltip_disarm(st);
}

bool region_drag_active(const struct ro_state *st) {
	return st->drawing || st->slider_dragging || st->moving_region ||
		   st->handle_dragging != HANDLE_NONE;
}

void region_drag_abort(struct ro_state *st) {
	if (st->drawing) {
		free(st->pen_points);
		st->pen_points = NULL;
		st->pen_n = st->pen_cap = 0;
		st->drawing = false;
	}
	st->slider_dragging = false;
	st->moving_region = false;
	st->handle_dragging = HANDLE_NONE;
	if (st->text_input_active) {
		st->text_input_active = false;
		st->text_len = 0;
	}
	region_undo_disarm(st);
}

bool region_set_hover(struct ro_state *st, int btn) {
	if (btn == st->hovered_button) return false;
	st->hovered_button = btn;
	bool was_visible = st->tooltip_visible;
	st->tooltip_visible = false;
	if (btn >= 0) region_tooltip_arm(st);
	else region_tooltip_disarm(st);
	return was_visible;
}

void region_update_selection(struct ro_state *st) {
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

bool region_inside_selection(const struct ro_state *st, int32_t x, int32_t y) {
	return x >= st->sel_x && y >= st->sel_y &&
		   x < st->sel_x + st->sel_w && y < st->sel_y + st->sel_h;
}

void region_pen_append(struct ro_state *st, int32_t x, int32_t y) {
	if (st->pen_n >= PEN_POINTS_MAX) return;
	if (st->pen_n == st->pen_cap) {
		size_t cap = st->pen_cap ? st->pen_cap * 2 : 64;
		if (cap > PEN_POINTS_MAX) cap = PEN_POINTS_MAX;
		int32_t *p = realloc(st->pen_points, cap * 2 * sizeof(int32_t));
		if (!p) return;
		st->pen_points = p;
		st->pen_cap = cap;
	}
	st->pen_points[st->pen_n * 2] = x;
	st->pen_points[st->pen_n * 2 + 1] = y;
	st->pen_n++;
}

void region_commit_drawing(struct ro_state *st) {
	struct annotation a = {0};
	a.tool = st->current_tool;
	a.color = st->current_color;
	a.width = st->current_width;
	a.font_size = ANNO_DEFAULT_FONT;

	if (st->current_tool == TOOL_PEN) {
		if (st->pen_n == 0) {
			st->drawing = false;
			return;
		}
		a.points = st->pen_points;
		a.n_points = st->pen_n;
		st->pen_points = NULL;
		st->pen_n = st->pen_cap = 0;
	} else {
		int32_t x0 = st->draw_x0;
		int32_t y0 = st->draw_y0;
		int32_t x1 = st->cursor_x;
		int32_t y1 = st->cursor_y;
		if (st->shift_held) {
			if (st->current_tool == TOOL_RECT ||
				st->current_tool == TOOL_ELLIPSE ||
				st->current_tool == TOOL_BLUR) {
				int32_t dx = x1 - x0, dy = y1 - y0;
				int32_t adx = dx < 0 ? -dx : dx;
				int32_t ady = dy < 0 ? -dy : dy;
				int32_t side = adx > ady ? adx : ady;
				x1 = x0 + (dx < 0 ? -side : side);
				y1 = y0 + (dy < 0 ? -side : side);
			} else if (st->current_tool == TOOL_ARROW) {
				double angle = atan2((double)(y1 - y0), (double)(x1 - x0));
				double snap = round(angle / (M_PI / 4.0)) * (M_PI / 4.0);
				double dx = x1 - x0, dy = y1 - y0;
				double len = sqrt(dx * dx + dy * dy);
				x1 = x0 + (int32_t)(len * cos(snap));
				y1 = y0 + (int32_t)(len * sin(snap));
			}
		}
		a.x0 = x0;
		a.y0 = y0;
		a.x1 = x1;
		a.y1 = y1;
	}

	if (annotation_list_push(st->out_annos, &a) != 0) annotation_free(&a);
	st->drawing = false;
}

void region_commit_text(struct ro_state *st) {
	if (!st->text_input_active || st->text_len == 0) {
		st->text_input_active = false;
		st->text_len = 0;
		return;
	}
	struct annotation a = {0};
	a.tool = TOOL_TEXT;
	a.color = st->current_color;
	a.font_size = ANNO_DEFAULT_FONT;
	a.x0 = st->text_x;
	a.y0 = st->text_y;
	st->text_buf[st->text_len] = '\0';
	a.text = strdup(st->text_buf);
	if (!a.text || annotation_list_push(st->out_annos, &a) != 0) {
		annotation_free(&a);
	}
	st->text_input_active = false;
	st->text_len = 0;
}
