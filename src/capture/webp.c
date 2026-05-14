// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/save.h"
#include "log.h"

#ifdef HAVE_WEBP

#include <cairo/cairo.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <webp/encode.h>

int grabit_save_webp_surface(cairo_surface_t *surface, const char *path,
							 int quality, bool lossless) {
	if (!surface || !path) return -1;
	if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		log_error("webp: bad input surface");
		return -1;
	}

	cairo_surface_flush(surface);
	int w = cairo_image_surface_get_width(surface);
	int h = cairo_image_surface_get_height(surface);
	int stride = cairo_image_surface_get_stride(surface);
	const unsigned char *src = cairo_image_surface_get_data(surface);
	if (w <= 0 || h <= 0 || stride <= 0 || !src) {
		log_error("webp: empty surface");
		return -1;
	}

	if (quality < 0) quality = 0;
	if (quality > 100) quality = 100;

	int row_bytes = w * 4;
	unsigned char *rgba = malloc((size_t)row_bytes * (size_t)h);
	if (!rgba) return -1;

	for (int y = 0; y < h; y++) {
		const uint32_t *in = (const uint32_t *)(src + (size_t)y * (size_t)stride);
		unsigned char *out = rgba + (size_t)y * (size_t)row_bytes;
		for (int x = 0; x < w; x++) {
			uint32_t px = in[x];
			unsigned a = (px >> 24) & 0xff;
			unsigned r = (px >> 16) & 0xff;
			unsigned g = (px >> 8) & 0xff;
			unsigned b = px & 0xff;
			if (a == 0) {
				out[x * 4 + 0] = 0;
				out[x * 4 + 1] = 0;
				out[x * 4 + 2] = 0;
			} else if (a == 255) {
				out[x * 4 + 0] = (unsigned char)r;
				out[x * 4 + 1] = (unsigned char)g;
				out[x * 4 + 2] = (unsigned char)b;
			} else {
				out[x * 4 + 0] = (unsigned char)((r * 255 + a / 2) / a);
				out[x * 4 + 1] = (unsigned char)((g * 255 + a / 2) / a);
				out[x * 4 + 2] = (unsigned char)((b * 255 + a / 2) / a);
			}
			out[x * 4 + 3] = (unsigned char)a;
		}
	}

	uint8_t *encoded = NULL;
	size_t encoded_size;
	if (lossless) {
		encoded_size = WebPEncodeLosslessRGBA(rgba, w, h, row_bytes, &encoded);
	} else {
		encoded_size = WebPEncodeRGBA(rgba, w, h, row_bytes, (float)quality, &encoded);
	}
	free(rgba);

	if (encoded_size == 0 || !encoded) {
		log_error("webp: encode failed");
		WebPFree(encoded);
		return -1;
	}

	FILE *f = fopen(path, "wb");
	if (!f) {
		log_error("webp: open %s: %s", path, strerror(errno));
		WebPFree(encoded);
		return -1;
	}
	size_t wr = fwrite(encoded, 1, encoded_size, f);
	WebPFree(encoded);
	if (wr != encoded_size) {
		log_error("webp: write %s: short write", path);
		fclose(f);
		return -1;
	}
	if (fclose(f) != 0) {
		log_error("webp: close %s: %s", path, strerror(errno));
		return -1;
	}
	return 0;
}

#else

int grabit_save_webp_surface(cairo_surface_t *surface, const char *path,
							 int quality, bool lossless) {
	(void)surface;
	(void)path;
	(void)quality;
	(void)lossless;
	log_error("webp: not compiled in (rebuild with libwebp-dev installed)");
	return -1;
}

#endif
