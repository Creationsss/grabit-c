// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"
#include "region/wlr_state.h"

#include "wl.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <cairo/cairo.h>

#define COLOR_PICKER_W 220
#define COLOR_PICKER_GRID_H 130
#define COLOR_PICKER_INPUT_H 26
#define COLOR_PICKER_INPUT_GAP 5
#define COLOR_PICKER_INPUT_BTN_GAP 4
#define COLOR_PICKER_PAD 6
#define COLOR_PICKER_GAP 8
#define COLOR_PICKER_SWATCH_PAD_X 5
#define COLOR_PICKER_SWATCH_PAD_Y 4
#define COLOR_PICKER_TEXT_GAP 8

static int32_t color_picker_total_h(void) {
	return COLOR_PICKER_GRID_H + COLOR_PICKER_INPUT_GAP + COLOR_PICKER_INPUT_H;
}

void region_color_picker_rect(const struct ro_state *st,
							  int32_t *out_x, int32_t *out_y,
							  int32_t *out_w, int32_t *out_h) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	int32_t bx, by, bw, bh;
	toolbar_btn_rect_local(TB_COLOR_CURRENT, tw, &bx, &by, &bw, &bh);
	int32_t pw = COLOR_PICKER_W;
	int32_t total_h = color_picker_total_h();
	int32_t btn_cx = tx + bx + bw / 2;
	int32_t want_x = btn_cx - pw / 2;
	int32_t out_left = o->x + 8;
	int32_t out_right = o->x + o->logical_width - 8;
	if (want_x < out_left) want_x = out_left;
	if (want_x + pw > out_right) want_x = out_right - pw;
	int32_t want_y = ty - COLOR_PICKER_GAP - total_h;
	if (want_y < o->y + 8) want_y = ty + th + COLOR_PICKER_GAP;
	*out_x = want_x;
	*out_y = want_y;
	*out_w = pw;
	*out_h = COLOR_PICKER_GRID_H;
}

void region_color_input_rect(const struct ro_state *st,
							 int32_t *out_x, int32_t *out_y,
							 int32_t *out_w, int32_t *out_h) {
	int32_t gx, gy, gw, gh;
	region_color_picker_rect(st, &gx, &gy, &gw, &gh);
	if (gw <= 0) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_x = gx;
	*out_y = gy + gh + COLOR_PICKER_INPUT_GAP;
	*out_w = gw - COLOR_PICKER_INPUT_H - COLOR_PICKER_INPUT_BTN_GAP;
	*out_h = COLOR_PICKER_INPUT_H;
}

void region_color_eyedropper_rect(const struct ro_state *st,
								  int32_t *out_x, int32_t *out_y,
								  int32_t *out_w, int32_t *out_h) {
	int32_t gx, gy, gw, gh;
	region_color_picker_rect(st, &gx, &gy, &gw, &gh);
	if (gw <= 0) {
		*out_x = *out_y = *out_w = *out_h = 0;
		return;
	}
	*out_x = gx + gw - COLOR_PICKER_INPUT_H;
	*out_y = gy + gh + COLOR_PICKER_INPUT_GAP;
	*out_w = COLOR_PICKER_INPUT_H;
	*out_h = COLOR_PICKER_INPUT_H;
}

static int hex_digit(char c) {
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

bool region_parse_hex_color(const char *s, uint32_t *out) {
	if (!s || !*s) return false;
	if (*s == '#') s++;
	size_t len = strlen(s);
	uint32_t v = 0;
	if (len == 6) {
		for (int i = 0; i < 6; i++) {
			int d = hex_digit(s[i]);
			if (d < 0) return false;
			v = (v << 4) | (uint32_t)d;
		}
		*out = v & 0xFFFFFFu;
		return true;
	}
	if (len == 3) {
		for (int i = 0; i < 3; i++) {
			int d = hex_digit(s[i]);
			if (d < 0) return false;
			uint32_t dd = ((uint32_t)d << 4) | (uint32_t)d;
			v = (v << 8) | dd;
		}
		*out = v & 0xFFFFFFu;
		return true;
	}
	return false;
}

static void hsl_to_rgb(double h, double s, double l, double *r, double *g, double *b) {
	if (s <= 0.0) {
		*r = *g = *b = l;
		return;
	}
	double q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
	double p = 2.0 * l - q;
	double hk = h / 360.0;
	double t[3] = {hk + 1.0 / 3.0, hk, hk - 1.0 / 3.0};
	double *out[3] = {r, g, b};
	for (int i = 0; i < 3; i++) {
		double tc = t[i];
		if (tc < 0) tc += 1.0;
		if (tc > 1) tc -= 1.0;
		double v;
		if (tc < 1.0 / 6.0)
			v = p + (q - p) * 6.0 * tc;
		else if (tc < 0.5)
			v = q;
		else if (tc < 2.0 / 3.0)
			v = p + (q - p) * (2.0 / 3.0 - tc) * 6.0;
		else
			v = p;
		*out[i] = v;
	}
}

bool region_color_picker_pick(const struct ro_state *st, int32_t abs_x, int32_t abs_y,
							  uint32_t *out_color) {
	int32_t px, py, pw, ph;
	region_color_picker_rect(st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return false;
	int32_t lx = abs_x - px;
	int32_t ly = abs_y - py;
	if (lx < 0) lx = 0;
	if (lx >= pw) lx = pw - 1;
	if (ly < 0) ly = 0;
	if (ly >= ph) ly = ph - 1;
	double h = (double)lx / (double)pw * 360.0;
	double l = 1.0 - (double)ly / (double)(ph - 1);
	double rd, gd, bd;
	hsl_to_rgb(h, 1.0, l, &rd, &gd, &bd);
	uint32_t r = (uint32_t)(rd * 255.0 + 0.5);
	uint32_t g = (uint32_t)(gd * 255.0 + 0.5);
	uint32_t b = (uint32_t)(bd * 255.0 + 0.5);
	if (r > 255) r = 255;
	if (g > 255) g = 255;
	if (b > 255) b = 255;
	*out_color = (r << 16) | (g << 8) | b;
	return true;
}

static void render_grid(cairo_t *cr, int32_t S, double dx, double dy, double dw, double dh) {
	cairo_pattern_t *rainbow = cairo_pattern_create_linear(dx, 0, dx + dw, 0);
	static const int N_HUE_STOPS = 6;
	for (int i = 0; i <= N_HUE_STOPS; i++) {
		double frac = (double)i / (double)N_HUE_STOPS;
		double rd, gd, bd;
		hsl_to_rgb(frac * 360.0, 1.0, 0.5, &rd, &gd, &bd);
		cairo_pattern_add_color_stop_rgb(rainbow, frac, rd, gd, bd);
	}
	cairo_set_source(cr, rainbow);
	cairo_rectangle(cr, dx, dy, dw, dh);
	cairo_fill(cr);
	cairo_pattern_destroy(rainbow);

	double mid_y = dy + dh * 0.5;

	cairo_pattern_t *top = cairo_pattern_create_linear(0, dy, 0, mid_y);
	cairo_pattern_add_color_stop_rgba(top, 0.0, 1, 1, 1, 1);
	cairo_pattern_add_color_stop_rgba(top, 1.0, 1, 1, 1, 0);
	cairo_set_source(cr, top);
	cairo_rectangle(cr, dx, dy, dw, dh * 0.5);
	cairo_fill(cr);
	cairo_pattern_destroy(top);

	cairo_pattern_t *bot = cairo_pattern_create_linear(0, mid_y, 0, dy + dh);
	cairo_pattern_add_color_stop_rgba(bot, 0.0, 0, 0, 0, 0);
	cairo_pattern_add_color_stop_rgba(bot, 1.0, 0, 0, 0, 1);
	cairo_set_source(cr, bot);
	cairo_rectangle(cr, dx, mid_y, dw, dh * 0.5);
	cairo_fill(cr);
	cairo_pattern_destroy(bot);

	cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, dx + 0.5 * S, dy + 0.5 * S, dw - (double)S, dh - (double)S);
	cairo_stroke(cr);
}

static void render_input(cairo_t *cr, const struct ro_output *o, int32_t S) {
	int32_t ix, iy, iw, ih;
	region_color_input_rect(o->st, &ix, &iy, &iw, &ih);
	double dix = (double)(ix - o->go->x) * S;
	double diy = (double)(iy - o->go->y) * S;
	double diw = (double)iw * S;
	double dih = (double)ih * S;

	cairo_set_source_rgba(cr, 0.12, 0.12, 0.12, 1);
	cairo_rectangle(cr, dix, diy, diw, dih);
	cairo_fill(cr);
	if (o->st->color_input_active) {
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
	} else {
		cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	}
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, dix + 0.5 * S, diy + 0.5 * S,
					diw - (double)S, dih - (double)S);
	cairo_stroke(cr);

	double sw = dih - 2.0 * COLOR_PICKER_SWATCH_PAD_Y * S;
	double sx = dix + (double)COLOR_PICKER_SWATCH_PAD_X * S;
	double sy = diy + (double)COLOR_PICKER_SWATCH_PAD_Y * S;
	uint32_t cur = o->st->current_color;
	cairo_set_source_rgba(cr,
						  ((cur >> 16) & 0xff) / 255.0,
						  ((cur >> 8) & 0xff) / 255.0,
						  (cur & 0xff) / 255.0, 1);
	cairo_rectangle(cr, sx, sy, sw, sw);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.35);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, sx + 0.5 * S, sy + 0.5 * S,
					sw - (double)S, sw - (double)S);
	cairo_stroke(cr);

	char text[16];
	if (o->st->color_input_active) {
		snprintf(text, sizeof text, "#%s", o->st->color_input_buf);
	} else {
		snprintf(text, sizeof text, "#%06X", cur & 0xFFFFFFu);
	}
	cairo_select_font_face(cr, "monospace",
						   CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
	cairo_set_font_size(cr, 14.0 * S);
	cairo_text_extents_t ext;
	cairo_text_extents(cr, text, &ext);
	double text_x = sx + sw + (double)COLOR_PICKER_TEXT_GAP * S;
	double text_y = diy + dih / 2.0 - ext.height / 2.0 - ext.y_bearing;
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	cairo_move_to(cr, text_x, text_y);
	cairo_show_text(cr, text);

	if (o->st->color_input_active) {
		double cx = text_x + ext.x_advance;
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 1);
		cairo_set_line_width(cr, 1.5 * S);
		cairo_move_to(cr, cx, diy + 4.0 * S);
		cairo_line_to(cr, cx, diy + dih - 4.0 * S);
		cairo_stroke(cr);
	}
}

static void render_eyedropper_btn(cairo_t *cr, const struct ro_output *o, int32_t S) {
	int32_t ex, ey, ew, eh;
	region_color_eyedropper_rect(o->st, &ex, &ey, &ew, &eh);
	double dex = (double)(ex - o->go->x) * S;
	double dey = (double)(ey - o->go->y) * S;
	double dew = (double)ew * S;
	double deh = (double)eh * S;
	if (o->st->eyedropper_mode) {
		cairo_set_source_rgba(cr, GRABIT_ACCENT_R, GRABIT_ACCENT_G, GRABIT_ACCENT_B, 0.92);
	} else {
		cairo_set_source_rgba(cr, 0.18, 0.18, 0.18, 1);
	}
	cairo_rectangle(cr, dex, dey, dew, deh);
	cairo_fill(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 0.25);
	cairo_set_line_width(cr, (double)S);
	cairo_rectangle(cr, dex + 0.5 * S, dey + 0.5 * S,
					dew - (double)S, deh - (double)S);
	cairo_stroke(cr);
	cairo_set_source_rgba(cr, 1, 1, 1, 1);
	toolbar_icon_color_picker(cr, dex + dew / 2.0, dey + deh / 2.0, deh * 0.7);
}

void region_color_picker_render(cairo_t *cr, const struct ro_output *o) {
	if (!o->st->color_picker_open) return;
	int32_t px, py, pw, ph;
	region_color_picker_rect(o->st, &px, &py, &pw, &ph);
	if (pw <= 0 || ph <= 0) return;
	int32_t tx, ty, tw, th;
	const struct grabit_output *go;
	region_toolbar_rect(o->st, &go, &tx, &ty, &tw, &th);
	(void)tx;
	(void)ty;
	(void)tw;
	(void)th;
	if (!go || go != o->go) return;

	int32_t S = o->scale;
	double dx = (double)(px - o->go->x) * S;
	double dy = (double)(py - o->go->y) * S;
	double dw = (double)pw * S;
	double dh = (double)ph * S;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

	double bg_x = dx - (double)COLOR_PICKER_PAD * S;
	double bg_y = dy - (double)COLOR_PICKER_PAD * S;
	double bg_w = dw + 2.0 * COLOR_PICKER_PAD * S;
	double bg_h = dh + (COLOR_PICKER_INPUT_GAP + COLOR_PICKER_INPUT_H +
						2 * COLOR_PICKER_PAD) *
						   S;
	cairo_set_source_rgba(cr, 0.06, 0.06, 0.06, 0.96);
	cairo_rectangle(cr, bg_x, bg_y, bg_w, bg_h);
	cairo_fill(cr);

	render_grid(cr, S, dx, dy, dw, dh);
	render_input(cr, o, S);
	render_eyedropper_btn(cr, o, S);

	cairo_restore(cr);
}
