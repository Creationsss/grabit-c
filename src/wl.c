// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "wl.h"

#include "log.h"
#include "region/region.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "wlr-data-control-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "wlr-screencopy-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

static void output_geometry(void *data, struct wl_output *wo, int32_t x, int32_t y,
							int32_t pw, int32_t ph, int32_t subpixel,
							const char *make, const char *model, int32_t transform) {
	(void)wo;
	(void)pw;
	(void)ph;
	(void)subpixel;
	struct grabit_output *o = data;
	free(o->make);
	free(o->model);
	o->make = make ? strdup(make) : NULL;
	o->model = model ? strdup(model) : NULL;
	o->x = x;
	o->y = y;
	o->transform = transform;
}

static void output_mode(void *data, struct wl_output *wo, uint32_t flags,
						int32_t w, int32_t h, int32_t refresh) {
	(void)wo;
	(void)refresh;
	struct grabit_output *o = data;
	if (flags & WL_OUTPUT_MODE_CURRENT) {
		o->width = w;
		o->height = h;
	}
}

static void output_done(void *data, struct wl_output *wo) {
	(void)data;
	(void)wo;
}

static void output_scale(void *data, struct wl_output *wo, int32_t factor) {
	(void)wo;
	struct grabit_output *o = data;
	o->scale = factor;
}

static void output_name(void *data, struct wl_output *wo, const char *name) {
	(void)wo;
	struct grabit_output *o = data;
	free(o->name);
	o->name = name ? strdup(name) : NULL;
}

static void output_description(void *data, struct wl_output *wo, const char *desc) {
	(void)data;
	(void)wo;
	(void)desc;
}

static const struct wl_output_listener wl_output_listener_g = {
	.geometry = output_geometry,
	.mode = output_mode,
	.done = output_done,
	.scale = output_scale,
	.name = output_name,
	.description = output_description,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
	(void)seat;
	struct grabit_wl_state *s = data;
	s->seat_caps = caps;
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener_g = {
	.capabilities = seat_capabilities,
	.name = seat_name,
};

static void xdg_output_logical_position(void *data, struct zxdg_output_v1 *xo,
										int32_t x, int32_t y) {
	(void)xo;
	struct grabit_output *o = data;
	o->x = x;
	o->y = y;
}

static void xdg_output_logical_size(void *data, struct zxdg_output_v1 *xo,
									int32_t w, int32_t h) {
	(void)xo;
	struct grabit_output *o = data;
	o->logical_width = w;
	o->logical_height = h;
}

static void xdg_output_done(void *data, struct zxdg_output_v1 *xo) {
	(void)data;
	(void)xo;
}

static void xdg_output_xname(void *data, struct zxdg_output_v1 *xo, const char *name) {
	(void)xo;
	struct grabit_output *o = data;
	free(o->name);
	o->name = name ? strdup(name) : NULL;
}

static void xdg_output_xdescription(void *data, struct zxdg_output_v1 *xo,
									const char *desc) {
	(void)data;
	(void)xo;
	(void)desc;
}

static const struct zxdg_output_v1_listener xdg_output_listener_g = {
	.logical_position = xdg_output_logical_position,
	.logical_size = xdg_output_logical_size,
	.done = xdg_output_done,
	.name = xdg_output_xname,
	.description = xdg_output_xdescription,
};

static int outputs_push(struct grabit_wl_state *s, struct grabit_output *o) {
	if (s->n_outputs == s->cap_outputs) {
		size_t cap = s->cap_outputs ? s->cap_outputs * 2 : 4;
		struct grabit_output **p = realloc(s->outputs, cap * sizeof *p);
		if (!p) return -1;
		s->outputs = p;
		s->cap_outputs = cap;
	}
	s->outputs[s->n_outputs++] = o;
	return 0;
}

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
							const char *interface, uint32_t version) {
	struct grabit_wl_state *s = data;

	if (strcmp(interface, wl_shm_interface.name) == 0) {
		s->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
		return;
	}

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		uint32_t v = version > 4 ? 4 : version;
		s->compositor = wl_registry_bind(reg, name, &wl_compositor_interface, v);
		return;
	}

	if (strcmp(interface, wl_seat_interface.name) == 0) {
		if (s->seat) return;
		uint32_t v = version > 3 ? 3 : version;
		s->seat = wl_registry_bind(reg, name, &wl_seat_interface, v);
		wl_seat_add_listener(s->seat, &seat_listener_g, s);
		return;
	}

	if (strcmp(interface, wl_output_interface.name) == 0) {
		struct grabit_output *o = calloc(1, sizeof *o);
		if (!o) return;
		o->state = s;
		o->scale = 1;
		o->global_name = name;
		uint32_t v = version > 4 ? 4 : version;
		o->wl_output = wl_registry_bind(reg, name, &wl_output_interface, v);
		wl_output_add_listener(o->wl_output, &wl_output_listener_g, o);
		if (outputs_push(s, o) != 0) {
			wl_output_destroy(o->wl_output);
			free(o);
		}
		return;
	}

	if (strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0) {
		uint32_t v = version > 3 ? 3 : version;
		s->screencopy_manager = wl_registry_bind(
			reg, name, &zwlr_screencopy_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwlr_data_control_manager_v1_interface.name) == 0) {
		uint32_t v = version > 2 ? 2 : version;
		s->data_control_manager = wl_registry_bind(
			reg, name, &zwlr_data_control_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		uint32_t v = version > 4 ? 4 : version;
		s->layer_shell = wl_registry_bind(
			reg, name, &zwlr_layer_shell_v1_interface, v);
		return;
	}

	if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		uint32_t v = version > 3 ? 3 : version;
		s->xdg_output_manager = wl_registry_bind(
			reg, name, &zxdg_output_manager_v1_interface, v);
		return;
	}

	if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
		s->relative_pointer_manager = wl_registry_bind(
			reg, name, &zwp_relative_pointer_manager_v1_interface, 1);
		return;
	}
}

static void registry_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
	(void)reg;
	struct grabit_wl_state *s = data;
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->dead || o->global_name != name) continue;
		o->dead = true;
		log_warn("output %s removed mid-session; recording will black-fill its region",
				 o->name ? o->name : "?");
		if (o->xdg_output) {
			zxdg_output_v1_destroy(o->xdg_output);
			o->xdg_output = NULL;
		}
		if (o->wl_output) {
			wl_output_destroy(o->wl_output);
			o->wl_output = NULL;
		}
		return;
	}
}

static const struct wl_registry_listener registry_listener_g = {
	.global = registry_global,
	.global_remove = registry_global_remove,
};

int grabit_wl_init(struct grabit_wl_state *s) {
	memset(s, 0, sizeof *s);

	s->display = wl_display_connect(NULL);
	if (!s->display) {
		const char *wd = getenv("WAYLAND_DISPLAY");
		log_error("could not connect to wayland (WAYLAND_DISPLAY=%s)",
				  wd ? wd : "(unset)");
		return -1;
	}

	s->registry = wl_display_get_registry(s->display);
	wl_registry_add_listener(s->registry, &registry_listener_g, s);

	if (wl_display_roundtrip(s->display) < 0) goto fail;

	if (!s->shm) {
		log_error("compositor doesn't advertise wl_shm");
		goto fail;
	}
	if (!s->compositor) {
		log_error("compositor doesn't advertise wl_compositor");
		goto fail;
	}
	if (!s->screencopy_manager) {
		log_error("compositor doesn't advertise zwlr_screencopy_manager_v1; "
				  "grabit only supports wlroots-based compositors");
		goto fail;
	}

	if (wl_display_roundtrip(s->display) < 0) goto fail;

	if (s->xdg_output_manager) {
		for (size_t i = 0; i < s->n_outputs; i++) {
			struct grabit_output *o = s->outputs[i];
			o->xdg_output = zxdg_output_manager_v1_get_xdg_output(
				s->xdg_output_manager, o->wl_output);
			zxdg_output_v1_add_listener(o->xdg_output, &xdg_output_listener_g, o);
		}
		if (wl_display_roundtrip(s->display) < 0) goto fail;
	}

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->scale <= 0) o->scale = 1;
		// fallback if xdg-output didn't fill in logical dims; bit 0 of wl_output.transform set => 90° rotation, swap w/h.
		bool rotated = (o->transform & 1) != 0;
		int32_t native_logical_w = (rotated ? o->height : o->width) / o->scale;
		int32_t native_logical_h = (rotated ? o->width : o->height) / o->scale;
		if (o->logical_width <= 0) o->logical_width = native_logical_w;
		if (o->logical_height <= 0) o->logical_height = native_logical_h;
	}

	if (s->n_outputs == 0) {
		log_error("no outputs reported by the compositor");
		goto fail;
	}
	return 0;

fail:
	grabit_wl_finish(s);
	return -1;
}

void grabit_wl_finish(struct grabit_wl_state *s) {
	if (!s) return;

	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (o->xdg_output) zxdg_output_v1_destroy(o->xdg_output);
		if (o->wl_output) wl_output_destroy(o->wl_output);
		free(o->name);
		free(o->make);
		free(o->model);
		free(o);
	}
	free(s->outputs);
	s->outputs = NULL;
	s->n_outputs = s->cap_outputs = 0;

	if (s->relative_pointer_manager) zwp_relative_pointer_manager_v1_destroy(s->relative_pointer_manager);
	if (s->xdg_output_manager) zxdg_output_manager_v1_destroy(s->xdg_output_manager);
	if (s->layer_shell) zwlr_layer_shell_v1_destroy(s->layer_shell);
	if (s->data_control_manager) zwlr_data_control_manager_v1_destroy(s->data_control_manager);
	if (s->screencopy_manager) zwlr_screencopy_manager_v1_destroy(s->screencopy_manager);
	if (s->seat) wl_seat_destroy(s->seat);
	if (s->compositor) wl_compositor_destroy(s->compositor);
	if (s->shm) wl_shm_destroy(s->shm);
	if (s->registry) wl_registry_destroy(s->registry);
	if (s->display) wl_display_disconnect(s->display);

	memset(s, 0, sizeof *s);
}

struct grabit_output *grabit_wl_primary_output(struct grabit_wl_state *s) {
	return s->n_outputs > 0 ? s->outputs[0] : NULL;
}

struct grabit_output *grabit_wl_output_by_name(struct grabit_wl_state *s, const char *name) {
	if (!name) return NULL;
	for (size_t i = 0; i < s->n_outputs; i++) {
		if (s->outputs[i]->name && strcmp(s->outputs[i]->name, name) == 0)
			return s->outputs[i];
	}
	return NULL;
}

struct grabit_output *grabit_wl_output_at(struct grabit_wl_state *s, int32_t x, int32_t y) {
	for (size_t i = 0; i < s->n_outputs; i++) {
		struct grabit_output *o = s->outputs[i];
		if (x >= o->x && y >= o->y &&
			x < o->x + o->logical_width &&
			y < o->y + o->logical_height)
			return o;
	}
	return NULL;
}

bool grabit_output_rect_intersect(const struct grabit_output *o, const struct rect *r,
								  int32_t *out_x, int32_t *out_y,
								  int32_t *out_w, int32_t *out_h) {
	int32_t lx = r->x > o->x ? r->x : o->x;
	int32_t ly = r->y > o->y ? r->y : o->y;
	int32_t rx = (r->x + r->w) < (o->x + o->logical_width)
					 ? (r->x + r->w)
					 : (o->x + o->logical_width);
	int32_t ry = (r->y + r->h) < (o->y + o->logical_height)
					 ? (r->y + r->h)
					 : (o->y + o->logical_height);
	int32_t iw = rx - lx;
	int32_t ih = ry - ly;
	if (iw <= 0 || ih <= 0) return false;
	*out_x = lx;
	*out_y = ly;
	*out_w = iw;
	*out_h = ih;
	return true;
}
