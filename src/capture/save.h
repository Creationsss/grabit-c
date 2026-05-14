// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CAPTURE_SAVE_H
#define GRABIT_CAPTURE_SAVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

struct image;

struct png_slice {
	const struct image *src;
	int32_t src_x, src_y, src_w, src_h;
	int32_t dst_x, dst_y, dst_w, dst_h;
};

enum grabit_image_format {
	GRABIT_FMT_PNG = 0,
	GRABIT_FMT_JPEG,
	GRABIT_FMT_WEBP,
};

struct grabit_save_opts {
	enum grabit_image_format format;
	int jpeg_quality;
	int webp_quality;
	bool webp_lossless;
};

const char *grabit_format_extension(enum grabit_image_format f);
int grabit_format_from_name(const char *name, enum grabit_image_format *out);

struct rect;
struct annotation_list;
int grabit_save_composite_annotated(int32_t dst_w, int32_t dst_h,
									const struct png_slice *slices, size_t n,
									const struct rect *region, int32_t scale,
									const struct annotation_list *annos,
									const struct grabit_save_opts *opts,
									const char *path);

int grabit_save_png_surface(cairo_surface_t *surface, const char *path);
int grabit_save_jpeg_surface(cairo_surface_t *surface, const char *path, int quality);
int grabit_save_webp_surface(cairo_surface_t *surface, const char *path,
							 int quality, bool lossless);

#endif
