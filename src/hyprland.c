// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "hyprland.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <json-c/json.h>

static int connect_socket(void) {
	const char *his = getenv("HYPRLAND_INSTANCE_SIGNATURE");
	if (!his || !his[0]) return -1;

	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (!xdg || !xdg[0]) return -1;

	char *path = NULL;
	if (grabit_xasprintf(&path, "%s/hypr/%s/.socket.sock", xdg, his) != 0) return -1;

	struct sockaddr_un addr = { .sun_family = AF_UNIX };
	if (strlen(path) >= sizeof addr.sun_path) {
		free(path);
		return -1;
	}
	memcpy(addr.sun_path, path, strlen(path) + 1);
	free(path);

	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) return -1;
	if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int write_all(int fd, const char *buf, size_t len) {
	while (len > 0) {
		ssize_t w = write(fd, buf, len);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		buf += w;
		len -= (size_t)w;
	}
	return 0;
}

static int read_all(int fd, struct grabit_buf *out) {
	char tmp[4096];
	for (;;) {
		ssize_t r = read(fd, tmp, sizeof tmp);
		if (r == 0) return 0;
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (grabit_buf_putn(out, tmp, (size_t)r) != 0) return -1;
		if (out->len > 1 << 20) return -1;
	}
}

static char *json_str_dup(struct json_object *obj, const char *key) {
	struct json_object *v;
	if (!json_object_object_get_ex(obj, key, &v)) return NULL;
	if (json_object_get_type(v) != json_type_string) return NULL;
	const char *s = json_object_get_string(v);
	return s ? strdup(s) : NULL;
}

int grabit_hyprland_active_window(char **class_out, char **title_out) {
	if (class_out) *class_out = NULL;
	if (title_out) *title_out = NULL;

	int fd = connect_socket();
	if (fd < 0) return -1;

	if (write_all(fd, "j/activewindow", strlen("j/activewindow")) != 0) {
		close(fd);
		return -1;
	}
	shutdown(fd, SHUT_WR);

	struct grabit_buf body = {0};
	int rc = read_all(fd, &body);
	close(fd);
	if (rc != 0 || !body.data) {
		grabit_buf_free(&body);
		return -1;
	}

	struct json_object *root = json_tokener_parse(body.data);
	grabit_buf_free(&body);
	if (!root || json_object_get_type(root) != json_type_object) {
		if (root) json_object_put(root);
		log_debug("hyprland: invalid JSON from activewindow");
		return -1;
	}

	if (class_out) *class_out = json_str_dup(root, "class");
	if (title_out) *title_out = json_str_dup(root, "title");
	json_object_put(root);
	return 0;
}
