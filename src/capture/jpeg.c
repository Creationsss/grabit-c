// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/save.h"
#include "log.h"

#ifdef HAVE_JPEG

#include <cairo/cairo.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jpeglib.h>

int grabit_save_jpeg_surface(cairo_surface_t *surface, const char *path, int quality) {
	if (!surface || !path) return -1;
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		log_error("jpeg: bad input surface");
		return -1;
	}

	cairo_surface_flush(surface);
	int w = cairo_image_surface_get_width(surface);
	int h = cairo_image_surface_get_height(surface);
	int stride = cairo_image_surface_get_stride(surface);
	const unsigned char *src = cairo_image_surface_get_data(surface);
	if (w <= 0 || h <= 0 || stride <= 0 || !src) {
		log_error("jpeg: empty surface");
		return -1;
	}

	if (quality < 1) quality = 1;
	if (quality > 100) quality = 100;

	FILE *f = fopen(path, "wb");
	if (!f) {
		log_error("jpeg: open %s: %s", path, strerror(errno));
		return -1;
	}

	unsigned char *row = malloc((size_t)w * 3);
	if (!row) {
		fclose(f);
		return -1;
	}

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	jpeg_stdio_dest(&cinfo, f);
	cinfo.image_width = (JDIMENSION)w;
	cinfo.image_height = (JDIMENSION)h;
	cinfo.input_components = 3;
	cinfo.in_color_space = JCS_RGB;
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, quality, TRUE);
	jpeg_start_compress(&cinfo, TRUE);

	for (int y = 0; y < h; y++) {
		const uint32_t *line = (const uint32_t *)(src + (size_t)y * (size_t)stride);
		for (int x = 0; x < w; x++) {
			uint32_t px = line[x];
			row[x * 3 + 0] = (unsigned char)((px >> 16) & 0xff);
			row[x * 3 + 1] = (unsigned char)((px >> 8) & 0xff);
			row[x * 3 + 2] = (unsigned char)(px & 0xff);
		}
		JSAMPROW rows[1] = {row};
		jpeg_write_scanlines(&cinfo, rows, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	free(row);
	if (fclose(f) != 0) {
		log_error("jpeg: close %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

#else

int grabit_save_jpeg_surface(cairo_surface_t *surface, const char *path, int quality) {
	(void)surface;
	(void)path;
	(void)quality;
	log_error("jpeg: not compiled in (rebuild with libjpeg-turbo-dev installed)");
	return -1;
}

#endif
