// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "wl.h"

#include <math.h>

#include <cairo/cairo.h>

static bool button_active(const struct ro_state *st, enum tb_action act) {
	switch (act) {
	case TB_TOOL_PEN:
		return st->current_tool == TOOL_PEN;
	case TB_TOOL_RECT:
		return st->current_tool == TOOL_RECT;
	case TB_TOOL_ELLIPSE:
		return st->current_tool == TOOL_ELLIPSE;
	case TB_TOOL_ARROW:
		return st->current_tool == TOOL_ARROW;
	case TB_TOOL_BLUR:
		return st->current_tool == TOOL_BLUR;
	case TB_TOOL_TEXT:
		return st->current_tool == TOOL_TEXT;
	case TB_TOOL_ERASER:
		return st->current_tool == TOOL_ERASER;
	case TB_COLOR_RED:
		return st->current_color == TOOLBAR_COLORS[0];
	case TB_COLOR_YELLOW:
		return st->current_color == TOOLBAR_COLORS[1];
	case TB_COLOR_GREEN:
		return st->current_color == TOOLBAR_COLORS[2];
	case TB_COLOR_BLUE:
		return st->current_color == TOOLBAR_COLORS[3];
	case TB_COLOR_BLACK:
		return st->current_color == TOOLBAR_COLORS[4];
	case TB_COLOR_WHITE:
		return st->current_color == TOOLBAR_COLORS[5];
	case TB_UNDO:
		return st->undo_held;
	default:
		return false;
	}
}

static void paint_button_bg(cairo_t *cr, enum tb_action act, bool active,
							double bxi, double byi, double bwi, double bhi, double pad) {
	double rr = 0.18, gg = 0.18, bb = 0.18, aa = 0.94;
	if (active) {
		rr = 1.0; gg = 0.45; bb = 0.28; aa = 0.92;
	} else if (act == TB_SAVE) {
		rr = 0.20; gg = 0.58; bb = 0.32; aa = 0.96;
	} else if (act == TB_CANCEL) {
		rr = 0.62; gg = 0.22; bb = 0.22; aa = 0.96;
	}
	cairo_set_source_rgba(cr, rr, gg, bb, aa);
	cairo_rectangle(cr, bxi + pad, byi + pad, bwi - pad * 2, bhi - pad * 2);
	cairo_fill(cr);
}

static void paint_slider(cairo_t *cr, const struct ro_state *st, int32_t S,
						 double bxi, double bwi, double cyi) {
	double pad_in = 10.0 * S;
	double tk_x0 = bxi + pad_in;
	double tk_x1 = bxi + bwi - pad_in;
	cairo_set_source_rgba(cr, 0.55, 0.55, 0.55, 1);
	cairo_set_line_width(cr, 2.0 * S);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
	cairo_move_to(cr, tk_x0, cyi);
	cairo_line_to(cr, tk_x1, cyi);
	cairo_stroke(cr);

	int32_t w = st->current_width;
	if (w < WIDTH_MIN) w = WIDTH_MIN;
	if (w > WIDTH_MAX) w = WIDTH_MAX;
	double frac = (double)(w - WIDTH_MIN) / (double)(WIDTH_MAX - WIDTH_MIN);
	double kx = tk_x0 + frac * (tk_x1 - tk_x0);
	double kr = ((double)w * 0.45 + 3.0) * S;
	cairo_set_source_rgba(cr, 1.0, 0.55, 0.32, 1);
	cairo_arc(cr, kx, cyi, kr, 0, 2.0 * M_PI);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.55);
	cairo_set_line_width(cr, 1.0 * S);
	cairo_arc(cr, kx, cyi, kr, 0, 2.0 * M_PI);
	cairo_stroke(cr);
}

static void paint_tool_icon(cairo_t *cr, enum tb_action act, double cxi, double cyi, double s_icon) {
	switch (act) {
	case TB_TOOL_PEN: toolbar_icon_pen(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_RECT: toolbar_icon_rect(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_ELLIPSE: toolbar_icon_ellipse(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_ARROW: toolbar_icon_arrow(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_BLUR: toolbar_icon_blur(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_TEXT: toolbar_icon_text(cr, cxi, cyi, s_icon); break;
	case TB_TOOL_ERASER: toolbar_icon_eraser(cr, cxi, cyi, s_icon); break;
	case TB_UNDO: toolbar_icon_undo(cr, cxi, cyi, s_icon); break;
	case TB_SAVE: toolbar_icon_save(cr, cxi, cyi, s_icon); break;
	case TB_CANCEL: toolbar_icon_cancel(cr, cxi, cyi, s_icon); break;
	default: break;
	}
}

void region_toolbar_render(cairo_t *cr, const struct ro_output *o) {
	int32_t S = o->scale;
	int32_t tx, ty, tw, th;
	const struct grabit_output *to;
	region_toolbar_rect(o->st, &to, &tx, &ty, &tw, &th);
	if (!to || to != o->go) return;

	double bx0 = (double)(tx - o->go->x) * S;
	double by0 = (double)(ty - o->go->y) * S;
	double bw = (double)tw * S;
	double bh = (double)th * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	cairo_set_source_rgba(cr, 0.08, 0.08, 0.08, 0.94);
	cairo_rectangle(cr, bx0, by0, bw, bh);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.16);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, bx0 + 0.5 * S, by0 + 0.5 * S,
					bw - (double)S, bh - (double)S);
	cairo_stroke(cr);

	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action act = (enum tb_action)i;
		int32_t bx_local, by_local, bw_local, bh_local;
		toolbar_btn_rect_local(act, tw, &bx_local, &by_local, &bw_local, &bh_local);
		double bxi = bx0 + (double)bx_local * S;
		double byi = by0 + (double)by_local * S;
		double bwi = (double)bw_local * S;
		double bhi = (double)bh_local * S;
		double pad = 3.0 * S;

		bool active = button_active(o->st, act);
		bool is_color = (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE);
		bool is_slider = (act == TB_WIDTH_SLIDER);

		if (!is_color && !is_slider) paint_button_bg(cr, act, active, bxi, byi, bwi, bhi, pad);

		double cxi = bxi + bwi / 2.0;
		double cyi = byi + bhi / 2.0;
		double s_icon = (double)bh_local * S * 0.6;

		if (is_color) {
			toolbar_color_swatch(cr, cxi, cyi, s_icon,
								 TOOLBAR_COLORS[act - TB_COLOR_RED], active);
			continue;
		}
		if (is_slider) {
			paint_slider(cr, o->st, S, bxi, bwi, cyi);
			continue;
		}

		cairo_set_source_rgba(cr, active ? 1.0 : 0.92,
							  active ? 1.0 : 0.92,
							  active ? 1.0 : 0.92, 1);
		paint_tool_icon(cr, act, cxi, cyi, s_icon);
	}

	cairo_restore(cr);
}

static const char *tooltip_text(enum tb_action act) {
	switch (act) {
	case TB_TOOL_PEN: return "Pen  (1)";
	case TB_TOOL_RECT: return "Rectangle  (2)";
	case TB_TOOL_ELLIPSE: return "Ellipse  (3)";
	case TB_TOOL_ARROW: return "Arrow  (4)";
	case TB_TOOL_BLUR: return "Blur  (5)";
	case TB_TOOL_TEXT: return "Text  (6)";
	case TB_TOOL_ERASER: return "Eraser  (7)";
	case TB_COLOR_RED: return "Red";
	case TB_COLOR_YELLOW: return "Yellow";
	case TB_COLOR_GREEN: return "Green";
	case TB_COLOR_BLUE: return "Blue";
	case TB_COLOR_BLACK: return "Black";
	case TB_COLOR_WHITE: return "White";
	case TB_WIDTH_SLIDER: return "Line width  (drag)";
	case TB_UNDO: return "Undo  (u, hold to repeat)";
	case TB_SAVE: return "Save  (Enter)";
	case TB_CANCEL: return "Cancel  (Esc)";
	default: return NULL;
	}
}

void region_toolbar_tooltip_render(cairo_t *cr, const struct ro_output *o) {
	const struct ro_state *st = o->st;
	if (!st->tooltip_visible || st->hovered_button < 0) return;
	const char *text = tooltip_text((enum tb_action)st->hovered_button);
	if (!text) return;

	int32_t tx, ty, tw, th;
	const struct grabit_output *to;
	region_toolbar_rect(st, &to, &tx, &ty, &tw, &th);
	if (!to || to != o->go) return;

	int32_t S = o->scale;
	double bx0 = (double)(tx - o->go->x) * S;
	double by0 = (double)(ty - o->go->y) * S;

	int32_t bx_local, by_local, bw_local, bh_local;
	toolbar_btn_rect_local((enum tb_action)st->hovered_button, tw,
						   &bx_local, &by_local, &bw_local, &bh_local);
	double btn_cx = bx0 + ((double)bx_local + (double)bw_local / 2.0) * S;
	double btn_top = by0 + (double)by_local * S;
	double btn_bot = btn_top + (double)bh_local * S;

	cairo_save(cr);
	cairo_select_font_face(cr, "sans-serif",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cr, 13.0 * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);

	double pad_x = 9.0 * S, pad_y = 6.0 * S;
	double tip_w = ext.width + pad_x * 2;
	double tip_h = ext.height + pad_y * 2;
	double gap = 8.0 * S;

	double tip_x = btn_cx - tip_w / 2.0;
	double tip_y = btn_top - gap - tip_h;
	bool below = false;
	if (tip_y < (double)S * 4.0) {
		tip_y = btn_bot + gap;
		below = true;
	}
	double pw = (double)o->pixel_width;
	if (tip_x < (double)S * 4.0) tip_x = (double)S * 4.0;
	if (tip_x + tip_w > pw - (double)S * 4.0) tip_x = pw - tip_w - (double)S * 4.0;

	double r = 4.0 * S;
	cairo_new_sub_path(cr);
	cairo_arc(cr, tip_x + r, tip_y + r, r, M_PI, 1.5 * M_PI);
	cairo_arc(cr, tip_x + tip_w - r, tip_y + r, r, 1.5 * M_PI, 2.0 * M_PI);
	cairo_arc(cr, tip_x + tip_w - r, tip_y + tip_h - r, r, 0.0, 0.5 * M_PI);
	cairo_arc(cr, tip_x + r, tip_y + tip_h - r, r, 0.5 * M_PI, M_PI);
	cairo_close_path(cr);
	cairo_set_source_rgba(cr, 0.04, 0.04, 0.04, 0.94);
	cairo_fill_preserve(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.18);
	cairo_set_line_width(cr, (double)S);
	cairo_stroke(cr);

	double arrow_h = 5.0 * S;
	double arrow_w = 9.0 * S;
	double ax = btn_cx;
	if (ax < tip_x + r + arrow_w / 2.0) ax = tip_x + r + arrow_w / 2.0;
	if (ax > tip_x + tip_w - r - arrow_w / 2.0) ax = tip_x + tip_w - r - arrow_w / 2.0;
	cairo_set_source_rgba(cr, 0.04, 0.04, 0.04, 0.94);
	if (below) {
		cairo_move_to(cr, ax - arrow_w / 2.0, tip_y);
		cairo_line_to(cr, ax + arrow_w / 2.0, tip_y);
		cairo_line_to(cr, ax, tip_y - arrow_h);
	} else {
		cairo_move_to(cr, ax - arrow_w / 2.0, tip_y + tip_h);
		cairo_line_to(cr, ax + arrow_w / 2.0, tip_y + tip_h);
		cairo_line_to(cr, ax, tip_y + tip_h + arrow_h);
	}
	cairo_close_path(cr);
	cairo_fill(cr);

	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, tip_x + pad_x - ext.x_bearing,
				  tip_y + pad_y - ext.y_bearing);
	cairo_show_text(cr, text);
	cairo_restore(cr);
}
