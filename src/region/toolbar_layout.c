// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "region/toolbar_internal.h"

#include "wl.h"

#include <stdbool.h>
#include <stddef.h>

const uint32_t TOOLBAR_COLORS[6] = {
	0xff3030u,
	0xfff030u,
	0x40ff40u,
	0x4080ffu,
	0x000000u,
	0xffffffu,
};

int toolbar_row_of(enum tb_action act) {
	if (act >= TB_COLOR_RED && act <= TB_WIDTH_SLIDER) return 1;
	return 0;
}

int32_t toolbar_btn_w(enum tb_action act) {
	if (act >= TB_COLOR_RED && act <= TB_COLOR_WHITE) return 26;
	if (act == TB_WIDTH_SLIDER) return TB_SLIDER_W;
	return TB_BTN_W;
}

int32_t toolbar_btn_h(enum tb_action act) {
	return toolbar_row_of(act) == 1 ? TB_BTN_H_OPT : TB_BTN_H;
}

static int32_t section_gap_before(enum tb_action act) {
	if (act == TB_WIDTH_SLIDER) return 12;
	if (act == TB_UNDO) return 10;
	return 2;
}

static int32_t row_total_w(int row) {
	int32_t x = 0;
	bool first = true;
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action a = (enum tb_action)i;
		if (toolbar_row_of(a) != row) continue;
		if (!first) x += section_gap_before(a);
		x += toolbar_btn_w(a);
		first = false;
	}
	return x;
}

static int32_t btn_x_in_row(enum tb_action act) {
	int row = toolbar_row_of(act);
	int32_t x = 0;
	bool first = true;
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		enum tb_action a = (enum tb_action)i;
		if (toolbar_row_of(a) != row) continue;
		if (a == act) return x;
		if (!first) x += section_gap_before(a);
		x += toolbar_btn_w(a);
		first = false;
	}
	return x;
}

static int32_t row_y_offset(int row) {
	if (row == 1) return TB_PAD + TB_BTN_H + TB_ROW_GAP;
	return TB_PAD;
}

static int32_t tb_total_w(void) {
	int32_t r0 = row_total_w(0);
	int32_t r1 = row_total_w(1);
	int32_t mx = r0 > r1 ? r0 : r1;
	return mx + TB_PAD * 2;
}

static int32_t tb_total_h(void) {
	return TB_PAD + TB_BTN_H + TB_ROW_GAP + TB_BTN_H_OPT + TB_PAD;
}

void toolbar_btn_rect_local(enum tb_action act, int32_t tw,
							int32_t *out_x, int32_t *out_y,
							int32_t *out_w, int32_t *out_h) {
	int row = toolbar_row_of(act);
	int32_t row_w = row_total_w(row);
	int32_t row_x0 = (tw - row_w) / 2;
	*out_x = row_x0 + btn_x_in_row(act);
	*out_y = row_y_offset(row);
	*out_w = toolbar_btn_w(act);
	*out_h = toolbar_btn_h(act);
}

static const struct grabit_output *output_for_selection_center(const struct ro_state *st) {
	int32_t cx = st->sel_x + st->sel_w / 2;
	int32_t cy = st->sel_y + st->sel_h / 2;
	for (size_t i = 0; i < st->n_outs; i++) {
		const struct grabit_output *o = st->outs[i].go;
		if (cx >= o->x && cy >= o->y &&
			cx < o->x + o->logical_width &&
			cy < o->y + o->logical_height) return o;
	}
	if (st->n_outs > 0) return st->outs[0].go;
	return NULL;
}

void region_toolbar_rect(const struct ro_state *st,
						 const struct grabit_output **out_o,
						 int32_t *x, int32_t *y, int32_t *w, int32_t *h) {
	const struct grabit_output *o = output_for_selection_center(st);
	if (out_o) *out_o = o;
	if (!o) {
		*x = *y = *w = *h = 0;
		return;
	}
	int32_t tw = tb_total_w();
	int32_t th = tb_total_h();
	*w = tw;
	*h = th;

	int32_t sel_bottom = st->sel_y + st->sel_h;
	int32_t sel_top = st->sel_y;
	int32_t out_bottom = o->y + o->logical_height;
	int32_t out_top = o->y;

	if (sel_bottom + TB_GAP + th <= out_bottom) {
		*y = sel_bottom + TB_GAP;
	} else if (st->sel_h >= th + TB_GAP * 2) {
		*y = sel_bottom - th - TB_GAP;
	} else if (sel_top - TB_GAP - th >= out_top) {
		*y = sel_top - TB_GAP - th;
	} else {
		*y = out_bottom - th - TB_GAP;
		if (*y < out_top + TB_GAP) *y = out_top + TB_GAP;
	}

	int32_t want_x = st->sel_x + st->sel_w / 2 - tw / 2;
	int32_t out_left = o->x + 8;
	int32_t out_right = o->x + o->logical_width - tw - 8;
	if (want_x < out_left) want_x = out_left;
	if (want_x > out_right) want_x = out_right;
	*x = want_x;
}

void region_toolbar_slider_rect(const struct ro_state *st,
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
	toolbar_btn_rect_local(TB_WIDTH_SLIDER, tw, &bx, &by, &bw, &bh);
	*out_x = tx + bx + 10;
	*out_y = ty + by;
	*out_w = bw - 20;
	*out_h = bh;
}

bool region_toolbar_contains(const struct ro_state *st, int32_t abs_x, int32_t abs_y) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) return false;
	return abs_x >= tx && abs_x < tx + tw && abs_y >= ty && abs_y < ty + th;
}

enum tb_action region_toolbar_hit(const struct ro_state *st,
								  int32_t abs_x, int32_t abs_y) {
	int32_t tx, ty, tw, th;
	const struct grabit_output *o;
	region_toolbar_rect(st, &o, &tx, &ty, &tw, &th);
	if (!o) return TB_NONE;
	int32_t local_x = abs_x - tx;
	int32_t local_y = abs_y - ty;
	for (int i = 0; i < TB_BTN_COUNT; i++) {
		int32_t bx, by, bw, bh;
		toolbar_btn_rect_local((enum tb_action)i, tw, &bx, &by, &bw, &bh);
		if (local_x >= bx && local_x < bx + bw &&
			local_y >= by && local_y < by + bh) return (enum tb_action)i;
	}
	return TB_NONE;
}
