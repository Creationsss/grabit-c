// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_FREEZE_H
#define GRABIT_CAPTURE_FREEZE_H

struct grabit_wl_state;
struct rect;

int grabit_freeze_capture(struct grabit_wl_state *s, const char *path, struct rect *out_rect);

#endif
