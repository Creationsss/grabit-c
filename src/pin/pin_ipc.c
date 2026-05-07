// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "pin/pin.h"
#include "pin/pin_state.h"

#include "log.h"
#include "notify/notify.h"
#include "util.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define PIN_SOCK_PREFIX "grabit_pin-"
#define PIN_SOCK_SUFFIX ".sock"

static void set_cloexec(int fd) {
	int fl = fcntl(fd, F_GETFD);
	if (fl >= 0) fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}

static void set_cloexec_nonblock(int fd) {
	set_cloexec(fd);
	int sl = fcntl(fd, F_GETFL);
	if (sl >= 0) fcntl(fd, F_SETFL, sl | O_NONBLOCK);
}

int pin_ipc_open(struct pin_state *st) {
	char dir[200];
	if (grabit_runtime_dir(dir, sizeof dir) != 0) return -1;

	int n = snprintf(st->ipc_path, sizeof st->ipc_path,
					 "%s/" PIN_SOCK_PREFIX "%d" PIN_SOCK_SUFFIX, dir, (int)getpid());
	if (n <= 0 || (size_t)n >= sizeof st->ipc_path) return -1;

	unlink(st->ipc_path);

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		log_error("pin: socket: %s", strerror(errno));
		return -1;
	}
	set_cloexec_nonblock(fd);

	struct sockaddr_un addr = {0};
	addr.sun_family = AF_UNIX;
	if (strlen(st->ipc_path) >= sizeof addr.sun_path) {
		close(fd);
		return -1;
	}
	memcpy(addr.sun_path, st->ipc_path, strlen(st->ipc_path) + 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
		log_error("pin: bind %s: %s", st->ipc_path, strerror(errno));
		close(fd);
		return -1;
	}
	chmod(st->ipc_path, 0600);
	st->ipc_fd = fd;
	return 0;
}

void pin_ipc_close(struct pin_state *st) {
	if (st->ipc_fd >= 0) {
		close(st->ipc_fd);
		st->ipc_fd = -1;
	}
	if (st->ipc_path[0]) {
		unlink(st->ipc_path);
		st->ipc_path[0] = '\0';
	}
}

void pin_ipc_handle(struct pin_state *st) {
	for (;;) {
		char buf[64];
		ssize_t n = recv(st->ipc_fd, buf, sizeof buf - 1, 0);
		if (n < 0) {
			if (errno == EINTR) continue;
			return;
		}
		if (n == 0) return;
		buf[n] = '\0';
		char *nl = strchr(buf, '\n');
		if (nl) *nl = '\0';
		if (strcmp(buf, "grab") == 0) {
			if (!st->input_grabbed) {
				st->input_grabbed = true;
				pin_input_apply_region(st);
				pin_render_repaint_button_area(st);
				pin_input_refresh_cursor(st);
			}
		} else if (strcmp(buf, "release") == 0) {
			if (st->input_grabbed) {
				st->input_grabbed = false;
				st->dragging = false;
				st->pending_dx_fixed = 0;
				st->pending_dy_fixed = 0;
				pin_input_apply_region(st);
				pin_render_repaint_button_area(st);
				pin_input_refresh_cursor(st);
			}
		} else if (strcmp(buf, "close") == 0) {
			st->finished = true;
		}
	}
}

static int parse_pid_from_socket_name(const char *name, pid_t *out) {
	const size_t prefix_len = sizeof PIN_SOCK_PREFIX - 1;
	const size_t suffix_len = sizeof PIN_SOCK_SUFFIX - 1;
	if (strncmp(name, PIN_SOCK_PREFIX, prefix_len) != 0) return -1;
	const char *p = name + prefix_len;
	char *end = NULL;
	long v = strtol(p, &end, 10);
	if (!end || strlen(end) != suffix_len) return -1;
	if (strcmp(end, PIN_SOCK_SUFFIX) != 0) return -1;
	if (v <= 0) return -1;
	*out = (pid_t)v;
	return 0;
}

int pin_ipc_broadcast(const char *msg) {
	char dir[200];
	if (grabit_runtime_dir(dir, sizeof dir) != 0) return -1;

	DIR *d = opendir(dir);
	if (!d) return 0;

	int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	if (fd < 0) {
		closedir(d);
		log_error("pin: socket: %s", strerror(errno));
		return -1;
	}
	set_cloexec(fd);

	int sent = 0;
	struct dirent *ent;
	while ((ent = readdir(d))) {
		pid_t pid;
		if (parse_pid_from_socket_name(ent->d_name, &pid) != 0) continue;

		char path[320];
		int pn = snprintf(path, sizeof path, "%s/%s", dir, ent->d_name);
		if (pn <= 0 || (size_t)pn >= sizeof path) continue;

		if (!grabit_process_alive(pid) || !grabit_is_grabit_process(pid)) {
			unlink(path);
			continue;
		}

		struct sockaddr_un addr = {0};
		addr.sun_family = AF_UNIX;
		if (strlen(path) >= sizeof addr.sun_path) continue;
		memcpy(addr.sun_path, path, strlen(path) + 1);

		size_t mlen = strlen(msg);
		if (sendto(fd, msg, mlen, 0,
				   (struct sockaddr *)&addr, sizeof addr) == (ssize_t)mlen) {
			sent++;
		}
	}
	close(fd);
	closedir(d);
	return sent;
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
	char body[64];
	snprintf(body, sizeof body, "%d pin%s closed", n, n == 1 ? "" : "s");
	notify_send(&(struct notify_opts){
		.summary = "grabit",
		.body = body,
	});
	return 0;
}
