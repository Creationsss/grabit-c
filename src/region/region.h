// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_REGION_H
#define GRABIT_REGION_REGION_H

#include <stdint.h>

struct grabit_wl_state;
struct image;

struct rect {
	int32_t x;
	int32_t y;
	int32_t w;
	int32_t h;
};

int region_select(struct grabit_wl_state *s, const struct image *frozen_per_output, struct rect *out);

#endif
