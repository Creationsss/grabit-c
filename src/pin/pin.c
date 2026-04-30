// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin.h"

#include "log.h"
#include "notify/notify.h"
#include "pin/pin_state.h"
#include "region/region.h"
#include "wl.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cairo/cairo.h>
#include <wayland-client.h>

#include "relative-pointer-unstable-v1-client-protocol.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

static volatile sig_atomic_t g_term = 0;
static void on_term(int sig) {
	(void)sig;
	g_term = 1;
}

static void compute_centered_jitter(int32_t img_w, int32_t img_h,
									int32_t out_w, int32_t out_h,
									int32_t *mx_out, int32_t *my_out) {
	int32_t cx = (out_w - img_w) / 2;
	int32_t cy = (out_h - img_h) / 2;
	uint32_t seed = (uint32_t)getpid() ^ (uint32_t)time(NULL);
	int32_t jx = (int32_t)((seed * 2654435761u) % 241u) - 120;
	int32_t jy = (int32_t)((seed * 40503u) % 241u) - 120;
	int32_t mx = cx + jx;
	int32_t my = cy + jy;
	if (mx < 0) mx = 0;
	if (my < 0) my = 0;
	*mx_out = mx;
	*my_out = my;
}

static int pin_main(cairo_surface_t *img, bool have_rect, struct rect r) {
	struct grabit_wl_state wls;
	if (grabit_wl_init(&wls) != 0) return 1;
	if (!wls.layer_shell || !wls.compositor) {
		grabit_wl_finish(&wls);
		return 1;
	}

	struct pin_state st = {0};
	st.wls = &wls;
	st.image = img;
	st.img_w = cairo_image_surface_get_width(img);
	st.img_h = cairo_image_surface_get_height(img);
	st.scale = 1;
	st.ipc_fd = -1;

	struct grabit_output *target = NULL;
	if (have_rect) {
		target = grabit_wl_output_at(&wls, r.x, r.y);
	}
	if (!target) target = grabit_wl_primary_output(&wls);

	if (target && target->scale > 0) st.scale = target->scale;

	if (have_rect) {
		st.width = r.w;
		st.height = r.h;
	} else {
		st.width = st.img_w / st.scale;
		st.height = st.img_h / st.scale;
		if (st.width <= 0) st.width = st.img_w;
		if (st.height <= 0) st.height = st.img_h;
	}

	if (have_rect && target) {
		st.margin_x = r.x - target->x;
		st.margin_y = r.y - target->y;
		if (st.margin_x < 0) st.margin_x = 0;
		if (st.margin_y < 0) st.margin_y = 0;
	} else {
		int32_t out_w = target ? target->logical_width : 1920;
		int32_t out_h = target ? target->logical_height : 1080;
		compute_centered_jitter(st.width, st.height, out_w, out_h,
								&st.margin_x, &st.margin_y);
	}

	st.surface = wl_compositor_create_surface(wls.compositor);
	st.layer_surface = zwlr_layer_shell_v1_get_layer_surface(
		wls.layer_shell, st.surface,
		target ? target->wl_output : NULL,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "grabit-pin");

	pin_render_attach_layer(&st);
	pin_input_attach(&st);

	zwlr_layer_surface_v1_set_size(st.layer_surface,
								   (uint32_t)st.width, (uint32_t)st.height);
	zwlr_layer_surface_v1_set_anchor(st.layer_surface,
									 ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
										 ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_margin(st.layer_surface,
									 st.margin_y, 0, 0, st.margin_x);
	zwlr_layer_surface_v1_set_exclusive_zone(st.layer_surface, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(
		st.layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

	struct wl_region *empty = wl_compositor_create_region(wls.compositor);
	wl_surface_set_input_region(st.surface, empty);
	wl_region_destroy(empty);

	wl_surface_commit(st.surface);

	if (pin_ipc_open(&st) != 0) {
		log_warn("pin: ipc disabled (grab/release won't reach this pin)");
	}

	struct sigaction sa = {0};
	sa.sa_handler = on_term;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	while (!st.finished && !g_term) {
		while (wl_display_prepare_read(wls.display) != 0) {
			if (wl_display_dispatch_pending(wls.display) < 0) goto out;
		}
		if (wl_display_flush(wls.display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(wls.display);
			goto out;
		}

		struct pollfd pfds[2];
		int nfds = 0;
		pfds[nfds].fd = wl_display_get_fd(wls.display);
		pfds[nfds].events = POLLIN;
		nfds++;
		if (st.ipc_fd >= 0) {
			pfds[nfds].fd = st.ipc_fd;
			pfds[nfds].events = POLLIN;
			nfds++;
		}

		int pr = poll(pfds, (nfds_t)nfds, -1);
		if (pr < 0) {
			wl_display_cancel_read(wls.display);
			if (errno == EINTR) continue;
			break;
		}

		if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			wl_display_cancel_read(wls.display);
			log_warn("pin: lost wayland connection");
			goto out;
		}

		if (pfds[0].revents & POLLIN) {
			if (wl_display_read_events(wls.display) < 0) goto out;
		} else {
			wl_display_cancel_read(wls.display);
		}
		if (wl_display_dispatch_pending(wls.display) < 0) goto out;

		if (st.ipc_fd >= 0 && nfds > 1 && (pfds[1].revents & POLLIN)) {
			pin_ipc_handle(&st);
		}
	}

out:
	pin_ipc_close(&st);
	if (st.drag_frame_cb) {
		wl_callback_destroy(st.drag_frame_cb);
		st.drag_frame_cb = NULL;
	}
	pin_render_free_buffer(&st);
	if (st.layer_surface) zwlr_layer_surface_v1_destroy(st.layer_surface);
	if (st.surface) wl_surface_destroy(st.surface);
	if (st.relative_pointer) zwp_relative_pointer_v1_destroy(st.relative_pointer);
	if (st.pointer) wl_pointer_release(st.pointer);
	wl_display_roundtrip(wls.display);
	grabit_wl_finish(&wls);
	if (st.image) cairo_surface_destroy(st.image);
	return 0;
}

static int probe_layer_shell(void) {
	struct grabit_wl_state probe;
	if (grabit_wl_init(&probe) != 0) {
		log_error("pin: cannot connect to wayland");
		return -1;
	}
	bool have_ls = probe.layer_shell != NULL;
	bool have_compositor = probe.compositor != NULL;
	grabit_wl_finish(&probe);
	if (!have_ls) {
		log_error("pin: compositor lacks zwlr_layer_shell_v1");
		return -1;
	}
	if (!have_compositor) {
		log_error("pin: compositor lacks wl_compositor");
		return -1;
	}
	return 0;
}

int pin_spawn(struct config *cfg, const char *path, const struct rect *r) {
	(void)cfg;
	if (!path) return -1;

	if (probe_layer_shell() != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "compositor lacks layer-shell; see terminal for details",
			.force = true,
		});
		return -1;
	}

	cairo_surface_t *img = cairo_image_surface_create_from_png(path);
	if (cairo_surface_status(img) != CAIRO_STATUS_SUCCESS) {
		log_error("pin: load %s: %s", path,
				  cairo_status_to_string(cairo_surface_status(img)));
		cairo_surface_destroy(img);
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not load captured image",
			.force = true,
		});
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("pin: fork: %s", strerror(errno));
		cairo_surface_destroy(img);
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not fork pin process",
			.force = true,
		});
		return -1;
	}
	if (pid == 0) {
		pid_t gp = fork();
		if (gp < 0) _exit(2);
		if (gp != 0) _exit(0);
		setsid();
		int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
		if (devnull >= 0) {
			dup2(devnull, STDIN_FILENO);
			dup2(devnull, STDOUT_FILENO);
			if (devnull > STDERR_FILENO) close(devnull);
		}
		struct rect rcopy = r ? *r : (struct rect){0};
		_exit(pin_main(img, r != NULL, rcopy));
	}
	int status = 0;
	while (waitpid(pid, &status, 0) < 0) {
		if (errno != EINTR) break;
	}
	cairo_surface_destroy(img);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		log_error("pin: detach failed");
		notify_send(&(struct notify_opts){
			.summary = "grabit: pin failed",
			.body = "could not detach pin process",
			.force = true,
		});
		return -1;
	}
	return 0;
}

int pin_grab(void) {
	int n = pin_ipc_broadcast("grab\n");
	if (n < 0) return 1;
	log_debug("pin: grab → %d pin(s)", n);
	return 0;
}

int pin_release(void) {
	int n = pin_ipc_broadcast("release\n");
	if (n < 0) return 1;
	log_debug("pin: release → %d pin(s)", n);
	return 0;
}

int pin_close_all(void) {
	int n = pin_ipc_broadcast("close\n");
	if (n < 0) return 1;
	log_info("pin: closed %d pin(s)", n);
	return 0;
}
