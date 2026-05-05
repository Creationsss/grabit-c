// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_WLR_INPUT_STATE_H
#define GRABIT_REGION_WLR_INPUT_STATE_H

#include <stdbool.h>
#include <stdint.h>

struct ro_state;

#define HANDLE_NONE -1
#define HANDLE_NW 0
#define HANDLE_N 1
#define HANDLE_NE 2
#define HANDLE_E 3
#define HANDLE_SE 4
#define HANDLE_S 5
#define HANDLE_SW 6
#define HANDLE_W 7
#define HANDLE_RADIUS 9

int region_handle_at(const struct ro_state *st, int32_t x, int32_t y);
void region_apply_handle_drag(struct ro_state *st);

void region_undo_arm(struct ro_state *st);
void region_undo_disarm(struct ro_state *st);

void region_tooltip_arm(struct ro_state *st);
void region_tooltip_disarm(struct ro_state *st);

void region_drag_start(struct ro_state *st);
bool region_drag_active(const struct ro_state *st);
void region_drag_abort(struct ro_state *st);

bool region_set_hover(struct ro_state *st, int btn);

void region_update_selection(struct ro_state *st);
bool region_inside_selection(const struct ro_state *st, int32_t x, int32_t y);
int32_t region_clamp_x(const struct ro_state *st, int32_t x);
int32_t region_clamp_y(const struct ro_state *st, int32_t y);

void region_pen_append(struct ro_state *st, int32_t x, int32_t y);
void region_commit_drawing(struct ro_state *st);
void region_commit_text(struct ro_state *st);

#endif
