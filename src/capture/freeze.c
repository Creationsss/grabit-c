// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "capture/freeze.h"

#include "capture/capture.h"
#include "capture/png.h"
#include "log.h"
#include "region/region.h"
#include "wl.h"

#include <stdint.h>
#include <stdlib.h>

int grabit_freeze_capture(struct grabit_wl_state *s, const char *path, struct rect *out_rect) {
	struct image *frozen = calloc(s->n_outputs, sizeof *frozen);
	if (!frozen) return -1;

	int rc = -1;
	size_t captured = 0;
	struct png_slice *slices = NULL;

	for (size_t i = 0; i < s->n_outputs; i++) {
		if (capture_output_full(s, s->outputs[i], &frozen[i]) != 0) {
			log_error("freeze: capture of %s failed",
					  s->outputs[i]->name ? s->outputs[i]->name : "?");
			goto cleanup;
		}
		captured = i + 1;
		if (image_apply_transform(&frozen[i], s->outputs[i]->transform) != 0) {
			log_warn("freeze: transform of %s failed; output may look skewed",
					 s->outputs[i]->name ? s->outputs[i]->name : "?");
		}
	}

	struct rect r;
	if (region_select(s, frozen, &r) != 0) {
		log_info("region selection cancelled");
		goto cleanup;
	}
	if (r.w <= 0 || r.h <= 0) {
		log_error("empty selection");
		goto cleanup;
	}

	int32_t max_scale = 1;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix, iy, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix, &iy, &iw, &ih)) continue;
		if (o->scale > max_scale) max_scale = o->scale;
	}

	int32_t dst_w = r.w * max_scale;
	int32_t dst_h = r.h * max_scale;

	slices = calloc(s->n_outputs, sizeof *slices);
	if (!slices) {
		log_error("oom: composite slices");
		goto cleanup;
	}
	size_t n_slices = 0;

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		int32_t ix0, iy0, iw, ih;
		if (!grabit_output_rect_intersect(o, &r, &ix0, &iy0, &iw, &ih)) continue;

		double sxr = o->logical_width > 0
						 ? (double)frozen[i].width / (double)o->logical_width
						 : 1.0;
		double syr = o->logical_height > 0
						 ? (double)frozen[i].height / (double)o->logical_height
						 : 1.0;

		struct png_slice *sl = &slices[n_slices++];
		sl->src = &frozen[i];
		sl->src_x = (int32_t)((ix0 - o->x) * sxr + 0.5);
		sl->src_y = (int32_t)((iy0 - o->y) * syr + 0.5);
		sl->src_w = (int32_t)(iw * sxr + 0.5);
		sl->src_h = (int32_t)(ih * syr + 0.5);
		if (sl->src_x + sl->src_w > frozen[i].width)
			sl->src_w = frozen[i].width - sl->src_x;
		if (sl->src_y + sl->src_h > frozen[i].height)
			sl->src_h = frozen[i].height - sl->src_y;

		sl->dst_x = (ix0 - r.x) * max_scale;
		sl->dst_y = (iy0 - r.y) * max_scale;
		sl->dst_w = iw * max_scale;
		sl->dst_h = ih * max_scale;
	}

	if (n_slices == 0) {
		log_error("selection doesn't intersect any output");
		goto cleanup;
	}

	rc = grabit_png_write_composite(dst_w, dst_h, slices, n_slices, path);

	if (rc == 0 && out_rect) *out_rect = r;

cleanup:
	free(slices);
	for (size_t i = 0; i < captured; i++)
		image_free(&frozen[i]);
	free(frozen);
	return rc;
}
