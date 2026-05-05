// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PNG_H
#define GRABIT_PNG_H

#include <stddef.h>
#include <stdint.h>

struct image;

struct png_slice {
	const struct image *src;
	int32_t src_x, src_y, src_w, src_h;
	int32_t dst_x, dst_y, dst_w, dst_h;
};

struct rect;
struct annotation_list;
int grabit_png_write_composite_annotated(int32_t dst_w, int32_t dst_h,
										 const struct png_slice *slices, size_t n,
										 const struct rect *region, int32_t scale,
										 const struct annotation_list *annos,
										 const char *path);

#endif
