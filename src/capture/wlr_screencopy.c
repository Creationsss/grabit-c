// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/capture.h"

#include "log.h"
#include "util.h"
#include "wl.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-screencopy-unstable-v1-client-protocol.h"

void image_free(struct image *img) {
	if (!img) return;
	free(img->bytes);
	memset(img, 0, sizeof *img);
}

struct sc_state {
	struct grabit_wl_state *wls;
	struct zwlr_screencopy_frame_v1 *frame;
	struct wl_buffer *buffer;
	void *map;
	size_t map_size;

	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
	bool y_invert;
	bool swap_rb;

	uint32_t advertised[16];
	size_t n_advertised;

	int status;
};

static const char *shm_format_name(uint32_t f) {
	switch (f) {
	case WL_SHM_FORMAT_ARGB8888:
		return "ARGB8888";
	case WL_SHM_FORMAT_XRGB8888:
		return "XRGB8888";
	case WL_SHM_FORMAT_ABGR8888:
		return "ABGR8888";
	case WL_SHM_FORMAT_XBGR8888:
		return "XBGR8888";
	case WL_SHM_FORMAT_RGBA8888:
		return "RGBA8888";
	case WL_SHM_FORMAT_RGBX8888:
		return "RGBX8888";
	case WL_SHM_FORMAT_BGRA8888:
		return "BGRA8888";
	case WL_SHM_FORMAT_BGRX8888:
		return "BGRX8888";
	case WL_SHM_FORMAT_RGB565:
		return "RGB565";
	default:
		return NULL;
	}
}

static void sc_buffer(void *data, struct zwlr_screencopy_frame_v1 *f,
					  uint32_t format, uint32_t w, uint32_t h, uint32_t stride) {
	(void)f;
	struct sc_state *c = data;

	const char *fname = shm_format_name(format);
	log_debug("wlr-screencopy: buffer format=%s (0x%08x) %ux%u stride=%u",
			  fname ? fname : "?", format, w, h, stride);

	if (c->n_advertised < sizeof c->advertised / sizeof c->advertised[0]) {
		c->advertised[c->n_advertised++] = format;
	}

	if (c->buffer) return;

	bool swap_rb = false;
	uint32_t use_format = format;
	switch (format) {
	case WL_SHM_FORMAT_XRGB8888:
	case WL_SHM_FORMAT_ARGB8888:
		break;
	case WL_SHM_FORMAT_XBGR8888:
		swap_rb = true;
		use_format = WL_SHM_FORMAT_XBGR8888;
		break;
	case WL_SHM_FORMAT_ABGR8888:
		swap_rb = true;
		use_format = WL_SHM_FORMAT_ABGR8888;
		break;
	default:
		return;
	}
	c->swap_rb = swap_rb;

	if (h > 0 && stride > SIZE_MAX / (size_t)h) {
		log_error("wlr-screencopy: %ux%u stride=%u overflows", w, h, stride);
		c->status = -1;
		return;
	}

	c->format = use_format;
	c->width = (int32_t)w;
	c->height = (int32_t)h;
	c->stride = (int32_t)stride;

	size_t size = (size_t)stride * h;
	int fd = grabit_shm_anon("grabit-screencopy", size);
	if (fd < 0) {
		c->status = -1;
		return;
	}

	c->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (c->map == MAP_FAILED) {
		log_error("mmap(%zu): %s", size, strerror(errno));
		close(fd);
		c->map = NULL;
		c->status = -1;
		return;
	}
	c->map_size = size;

	struct wl_shm_pool *pool = wl_shm_create_pool(c->wls->shm, fd, (int32_t)size);
	c->buffer = wl_shm_pool_create_buffer(pool, 0, (int32_t)w, (int32_t)h,
										  (int32_t)stride, use_format);
	wl_shm_pool_destroy(pool);
	close(fd);
}

static void sc_linux_dmabuf(void *data, struct zwlr_screencopy_frame_v1 *f,
							uint32_t fmt, uint32_t w, uint32_t h) {
	(void)data;
	(void)f;
	(void)fmt;
	(void)w;
	(void)h;
}

static void sc_buffer_done(void *data, struct zwlr_screencopy_frame_v1 *f) {
	struct sc_state *c = data;
	if (!c->buffer) {
		log_error("wlr-screencopy: compositor advertised no supported shm format "
				  "(want XRGB8888/ARGB8888/XBGR8888/ABGR8888)");
		for (size_t i = 0; i < c->n_advertised; i++) {
			const char *n = shm_format_name(c->advertised[i]);
			log_error("  saw: %s (0x%08x)", n ? n : "unknown", c->advertised[i]);
		}
		c->status = -1;
		return;
	}
	zwlr_screencopy_frame_v1_copy(f, c->buffer);
}

static void sc_flags(void *data, struct zwlr_screencopy_frame_v1 *f, uint32_t flags) {
	(void)f;
	struct sc_state *c = data;
	c->y_invert = (flags & ZWLR_SCREENCOPY_FRAME_V1_FLAGS_Y_INVERT) != 0;
}

static void sc_damage(void *data, struct zwlr_screencopy_frame_v1 *f,
					  uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
	(void)data;
	(void)f;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
}

static void sc_ready(void *data, struct zwlr_screencopy_frame_v1 *f,
					 uint32_t hi, uint32_t lo, uint32_t nsec) {
	(void)f;
	(void)hi;
	(void)lo;
	(void)nsec;
	struct sc_state *c = data;
	c->status = 1;
}

static void sc_failed(void *data, struct zwlr_screencopy_frame_v1 *f) {
	(void)f;
	struct sc_state *c = data;
	c->status = -1;
	log_error("wlr-screencopy: compositor reported capture failure");
}

static const struct zwlr_screencopy_frame_v1_listener sc_listener = {
	.buffer = sc_buffer,
	.flags = sc_flags,
	.ready = sc_ready,
	.failed = sc_failed,
	.damage = sc_damage,
	.linux_dmabuf = sc_linux_dmabuf,
	.buffer_done = sc_buffer_done,
};

static int dispatch_capture(struct grabit_wl_state *s, struct sc_state *c) {
	while (c->status == 0) {
		if (wl_display_dispatch(s->display) < 0) {
			log_error("wl_display_dispatch: lost connection");
			c->status = -1;
			break;
		}
	}
	if (c->status != 1 || !c->map) return -1;
	return 0;
}

static uint32_t resolved_format(const struct sc_state *c) {
	if (!c->swap_rb) return c->format;
	return (c->format == WL_SHM_FORMAT_XBGR8888) ? WL_SHM_FORMAT_XRGB8888
												 : WL_SHM_FORMAT_ARGB8888;
}

static void copy_capture(const struct sc_state *c, void *dst, int32_t dst_stride) {
	for (int32_t row = 0; row < c->height; row++) {
		int32_t src_row = c->y_invert ? (c->height - 1 - row) : row;
		const uint8_t *src = (const uint8_t *)c->map + (size_t)src_row * (size_t)c->stride;
		uint8_t *d = (uint8_t *)dst + (size_t)row * (size_t)dst_stride;
		if (c->swap_rb) {
			const uint32_t *s32 = (const uint32_t *)src;
			uint32_t *d32 = (uint32_t *)d;
			for (int32_t x = 0; x < c->width; x++) {
				uint32_t p = s32[x];
				d32[x] = (p & 0xff00ff00u) |
						 ((p & 0x00ff0000u) >> 16) |
						 ((p & 0x000000ffu) << 16);
			}
		} else {
			memcpy(d, src, (size_t)c->width * 4);
		}
	}
}

static void cleanup_capture(struct sc_state *c) {
	if (c->map) munmap(c->map, c->map_size);
	if (c->buffer) wl_buffer_destroy(c->buffer);
	if (c->frame) zwlr_screencopy_frame_v1_destroy(c->frame);
}

int capture_output_full(struct grabit_wl_state *s, struct grabit_output *o,
						struct image *out) {
	if (!s || !s->screencopy_manager || !o || !out) return -1;
	if (o->dead || !o->wl_output) return -1;
	memset(out, 0, sizeof *out);

	struct sc_state c = {.wls = s};
	c.frame = zwlr_screencopy_manager_v1_capture_output(
		s->screencopy_manager, 0, o->wl_output);
	if (!c.frame) {
		log_error("zwlr_screencopy_manager_v1_capture_output: NULL");
		return -1;
	}
	zwlr_screencopy_frame_v1_add_listener(c.frame, &sc_listener, &c);

	int rc = -1;
	if (dispatch_capture(s, &c) == 0) {
		out->width = c.width;
		out->height = c.height;
		out->stride = c.stride;
		out->format = resolved_format(&c);
		out->size = c.map_size;
		out->bytes = malloc(c.map_size);
		if (out->bytes) {
			copy_capture(&c, out->bytes, c.stride);
			rc = 0;
		}
	}

	cleanup_capture(&c);
	if (rc != 0) image_free(out);
	return rc;
}

int capture_output_region(struct grabit_wl_state *s, struct grabit_output *o,
						  int32_t x, int32_t y, int32_t w, int32_t h,
						  bool overlay_cursor,
						  struct image *out) {
	if (!s || !s->screencopy_manager || !o || !out) return -1;
	if (w <= 0 || h <= 0) return -1;
	if (o->dead || !o->wl_output) return -1;
	memset(out, 0, sizeof *out);

	struct sc_state c = {.wls = s};
	c.frame = zwlr_screencopy_manager_v1_capture_output_region(
		s->screencopy_manager, overlay_cursor ? 1 : 0, o->wl_output, x, y, w, h);
	if (!c.frame) {
		log_error("zwlr_screencopy_manager_v1_capture_output_region: NULL");
		return -1;
	}
	zwlr_screencopy_frame_v1_add_listener(c.frame, &sc_listener, &c);

	int rc = -1;
	if (dispatch_capture(s, &c) == 0) {
		out->width = c.width;
		out->height = c.height;
		out->stride = c.stride;
		out->format = resolved_format(&c);
		out->size = c.map_size;
		out->bytes = malloc(c.map_size);
		if (out->bytes) {
			copy_capture(&c, out->bytes, c.stride);
			rc = 0;
		}
	}

	cleanup_capture(&c);
	if (rc != 0) image_free(out);
	return rc;
}

int capture_output_region_into(struct grabit_wl_state *s, struct grabit_output *o,
							   int32_t x, int32_t y, int32_t w, int32_t h,
							   bool overlay_cursor,
							   void *dst, int32_t dst_stride, int32_t dst_h,
							   uint32_t *out_format) {
	if (!s || !s->screencopy_manager || !o || !dst) return -1;
	if (w <= 0 || h <= 0 || dst_stride <= 0 || dst_h <= 0) return -1;
	if (o->dead || !o->wl_output) return -1;

	struct sc_state c = {.wls = s};
	c.frame = zwlr_screencopy_manager_v1_capture_output_region(
		s->screencopy_manager, overlay_cursor ? 1 : 0, o->wl_output, x, y, w, h);
	if (!c.frame) {
		log_error("zwlr_screencopy_manager_v1_capture_output_region: NULL");
		return -1;
	}
	zwlr_screencopy_frame_v1_add_listener(c.frame, &sc_listener, &c);

	int rc = -1;
	if (dispatch_capture(s, &c) == 0) {
		if (c.height != dst_h || c.width * 4 != dst_stride) {
			log_error("capture: size mismatch (got %dx%d, dst stride=%d h=%d)",
					  c.width, c.height, dst_stride, dst_h);
		} else {
			copy_capture(&c, dst, dst_stride);
			if (out_format) *out_format = resolved_format(&c);
			rc = 0;
		}
	}

	cleanup_capture(&c);
	return rc;
}
