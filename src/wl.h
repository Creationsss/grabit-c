// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_WL_H
#define GRABIT_WL_H

#include <stddef.h>
#include <stdint.h>

#include <wayland-client.h>

struct zwlr_screencopy_manager_v1;
struct zwlr_data_control_manager_v1;
struct zwlr_layer_shell_v1;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct wl_compositor;

struct grabit_wl_state;

struct grabit_output {
	struct grabit_wl_state           *state;
	struct wl_output          *wl_output;
	struct zxdg_output_v1     *xdg_output;
	char                      *name;
	char                      *make;
	char                      *model;
	int32_t                    x, y;
	int32_t                    width;           // native panel pixels (wl_output.mode)
	int32_t                    height;          // native panel pixels (wl_output.mode)
	int32_t                    logical_width;   // post-transform logical (xdg_output preferred)
	int32_t                    logical_height;
	int32_t                    scale;
	int32_t                    transform;       // wl_output.transform; bit 0 set ⇒ 90° rotated
};

struct grabit_wl_state {
	struct wl_display                       *display;
	struct wl_registry                      *registry;
	struct wl_shm                           *shm;
	struct wl_seat                          *seat;
	uint32_t                                 seat_caps;
	struct wl_compositor                    *compositor;

	struct zwlr_screencopy_manager_v1       *screencopy_manager;
	struct zwlr_data_control_manager_v1     *data_control_manager;
	struct zwlr_layer_shell_v1              *layer_shell;
	struct zxdg_output_manager_v1           *xdg_output_manager;

	struct grabit_output                   **outputs;
	size_t                                   n_outputs;
	size_t                                   cap_outputs;
};

int  grabit_wl_init(struct grabit_wl_state *s);
void grabit_wl_finish(struct grabit_wl_state *s);

struct grabit_output *grabit_wl_primary_output(struct grabit_wl_state *s);
struct grabit_output *grabit_wl_output_by_name(struct grabit_wl_state *s, const char *name);
struct grabit_output *grabit_wl_output_at(struct grabit_wl_state *s, int32_t x, int32_t y);

#endif
