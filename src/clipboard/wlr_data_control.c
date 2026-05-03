// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard_internal.h"

#include "log.h"
#include "wl.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <wayland-client.h>

#include "wlr-data-control-unstable-v1-client-protocol.h"

struct clip_state {
	const void *bytes;
	size_t size;
	bool cancelled;
};

static void source_send(void *data, struct zwlr_data_control_source_v1 *src,
						const char *mime, int32_t fd) {
	(void)src;
	(void)mime;
	struct clip_state *st = data;

	signal(SIGPIPE, SIG_IGN);

	const uint8_t *p = st->bytes;
	size_t left = st->size;
	while (left > 0) {
		ssize_t w = write(fd, p, left);
		if (w < 0) {
			if (errno == EINTR) continue;
			break;
		}
		p += w;
		left -= (size_t)w;
	}
	close(fd);
}

static void source_cancelled(void *data, struct zwlr_data_control_source_v1 *src) {
	(void)src;
	struct clip_state *st = data;
	st->cancelled = true;
}

static const struct zwlr_data_control_source_v1_listener source_listener_g = {
	.send = source_send,
	.cancelled = source_cancelled,
};

__attribute__((noreturn)) static void clip_child(const void *bytes, size_t size,
												 const char *const *mimes, size_t n_mimes) {
	setsid();

	int devnull = open("/dev/null", O_RDWR | O_CLOEXEC);
	if (devnull >= 0) {
		dup2(devnull, STDIN_FILENO);
		dup2(devnull, STDOUT_FILENO);
		if (devnull > STDERR_FILENO) close(devnull);
	}

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) _exit(1);
	if (!s.data_control_manager || !s.seat) {
		log_error("clipboard: compositor lacks zwlr_data_control_manager_v1 or wl_seat");
		grabit_wl_finish(&s);
		_exit(1);
	}

	struct zwlr_data_control_device_v1 *dev =
		zwlr_data_control_manager_v1_get_data_device(s.data_control_manager, s.seat);

	struct zwlr_data_control_source_v1 *src =
		zwlr_data_control_manager_v1_create_data_source(s.data_control_manager);

	struct clip_state st = {.bytes = bytes, .size = size};
	zwlr_data_control_source_v1_add_listener(src, &source_listener_g, &st);

	for (size_t i = 0; i < n_mimes; i++) {
		zwlr_data_control_source_v1_offer(src, mimes[i]);
	}

	zwlr_data_control_device_v1_set_selection(dev, src);

	while (!st.cancelled) {
		if (wl_display_dispatch(s.display) < 0) break;
	}

	zwlr_data_control_source_v1_destroy(src);
	zwlr_data_control_device_v1_destroy(dev);
	grabit_wl_finish(&s);
	_exit(0);
}

int clipboard_send_bytes(const void *bytes, size_t size,
						 const char *const *mimes, size_t n_mimes) {
	if (!bytes || size == 0 || n_mimes == 0) return -1;

	{
		struct grabit_wl_state probe;
		if (grabit_wl_probe(&probe) != 0) {
			log_error("clipboard: cannot connect to wayland");
			return -1;
		}
		bool have_dc = probe.data_control_manager != NULL;
		bool have_seat = probe.seat != NULL;
		grabit_wl_finish(&probe);
		if (!have_dc) {
			log_error("clipboard: compositor lacks zwlr_data_control_manager_v1");
			return -1;
		}
		if (!have_seat) {
			log_error("clipboard: no wl_seat available");
			return -1;
		}
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("fork: %s", strerror(errno));
		return -1;
	}
	if (pid == 0) {
		clip_child(bytes, size, mimes, n_mimes);
	}
	(void)pid;
	return 0;
}
