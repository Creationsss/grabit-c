// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_COMPOSE_H
#define GRABIT_RECORD_COMPOSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct grabit_wl_state;
struct grabit_output;
struct rect;

struct rec_slice {
	struct grabit_output *out;
	int32_t src_x, src_y, src_w, src_h;
	int32_t dst_x, dst_y, dst_w, dst_h;
};

struct sc_pool;

struct rec_layout {
	struct rec_slice *slices;
	size_t n;
	int32_t dst_w;
	int32_t dst_h;
	int32_t dst_stride;

	void *slice_scratch;
	size_t slice_scratch_size;
	int32_t slice_scratch_w;
	int32_t slice_scratch_h;

	struct sc_pool *slice_caches;
};

int rec_layout_build(struct grabit_wl_state *s, struct rect r, struct rec_layout *out);

bool rec_layout_is_direct(const struct rec_layout *layout);

int rec_layout_capture_direct_into(struct grabit_wl_state *s, const struct rec_layout *layout,
								   bool cursor, void *dst, int32_t dst_stride, int32_t dst_h);
int rec_layout_capture_compose(struct grabit_wl_state *s, struct rec_layout *layout,
							   bool cursor, void *dst_buf);

void rec_layout_free(struct rec_layout *layout);

#endif
