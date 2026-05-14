// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_FREEZE_H
#define GRABIT_CAPTURE_FREEZE_H

#include <stdbool.h>
#include <stdint.h>

struct grabit_wl_state;
struct rect;
struct grabit_save_opts;

#define GRABIT_CAPTURE_CANCELLED (-2)

int grabit_freeze_capture(struct grabit_wl_state *s, const char *path,
						  const struct grabit_save_opts *save_opts,
						  struct rect *out_rect, bool annotate,
						  uint32_t *inout_color, int32_t *inout_width,
						  bool *out_choices_dirty);

#endif
