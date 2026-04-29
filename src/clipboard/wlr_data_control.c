// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard.h"

#include "log.h"
#include "wl.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
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

static int clipboard_set_bytes(const void *bytes, size_t size,
							   const char *const *mimes, size_t n_mimes) {
	if (!bytes || size == 0 || n_mimes == 0) return -1;

	{
		struct grabit_wl_state probe;
		if (grabit_wl_init(&probe) != 0) {
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

int clipboard_set_text(const char *text) {
	if (!text) return -1;
	static const char *const TEXT_MIMES[] = {
		"text/plain;charset=utf-8",
		"text/plain",
		"UTF8_STRING",
		"STRING",
		"TEXT",
	};
	return clipboard_set_bytes(text, strlen(text),
							   TEXT_MIMES,
							   sizeof TEXT_MIMES / sizeof TEXT_MIMES[0]);
}

// 100 mib — well above any sane screenshot, well below stress.
#define GRABIT_CLIP_MAX_FILE_SIZE ((size_t)100 * 1024 * 1024)

int clipboard_set_image_file(const char *path) {
	if (!path) return -1;

	FILE *f = fopen(path, "rb");
	if (!f) {
		log_error("clipboard: open %s: %s", path, strerror(errno));
		return -1;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return -1;
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return -1;
	}
	if ((size_t)sz > GRABIT_CLIP_MAX_FILE_SIZE) {
		log_error("clipboard: file %s is %ld bytes, larger than %zu-byte cap",
				  path, sz, GRABIT_CLIP_MAX_FILE_SIZE);
		fclose(f);
		return -1;
	}
	rewind(f);

	void *buf = malloc((size_t)sz);
	if (!buf) {
		fclose(f);
		return -1;
	}
	if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
		free(buf);
		fclose(f);
		log_error("clipboard: short read on %s", path);
		return -1;
	}
	fclose(f);

	static const char *const IMAGE_MIMES[] = {
		"image/png",
	};
	int rc = clipboard_set_bytes(buf, (size_t)sz,
								 IMAGE_MIMES,
								 sizeof IMAGE_MIMES / sizeof IMAGE_MIMES[0]);

	free(buf);
	return rc;
}
