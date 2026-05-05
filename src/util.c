// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util.h"

#include "log.h"

#include <wayland-client.h>

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

int grabit_xasprintf(char **out, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);
	if (n < 0) {
		va_end(ap2);
		*out = NULL;
		return -1;
	}
	*out = malloc((size_t)n + 1);
	if (!*out) {
		va_end(ap2);
		return -1;
	}
	vsnprintf(*out, (size_t)n + 1, fmt, ap2);
	va_end(ap2);
	return 0;
}

const char *grabit_basename(const char *path) {
	if (!path) return "";
	const char *slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

bool grabit_in_path(const char *bin) {
	if (!bin || !bin[0]) return false;
	if (strchr(bin, '/')) return access(bin, X_OK) == 0;
	const char *path = getenv("PATH");
	if (!path || !path[0]) return false;
	char buf[4096];
	const char *p = path;
	while (*p) {
		const char *colon = strchr(p, ':');
		size_t len = colon ? (size_t)(colon - p) : strlen(p);
		if (len > 0 && len + 1 + strlen(bin) + 1 <= sizeof buf) {
			int n = snprintf(buf, sizeof buf, "%.*s/%s", (int)len, p, bin);
			if (n > 0 && (size_t)n < sizeof buf && access(buf, X_OK) == 0) {
				return true;
			}
		}
		if (!colon) break;
		p = colon + 1;
	}
	return false;
}

int grabit_shm_anon(const char *tag, size_t size) {
	int fd = memfd_create(tag ? tag : "grabit", MFD_CLOEXEC);
	if (fd < 0) {
		log_error("memfd_create(%s): %s", tag ? tag : "grabit", strerror(errno));
		return -1;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		log_error("ftruncate(%zu): %s", size, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

int grabit_buf_grow(struct grabit_buf *b, size_t need) {
	if (b->cap >= need) return 0;
	size_t cap = b->cap ? b->cap : 256;
	while (cap < need)
		cap *= 2;
	char *p = realloc(b->data, cap);
	if (!p) return -1;
	b->data = p;
	b->cap = cap;
	return 0;
}

int grabit_buf_putn(struct grabit_buf *b, const void *s, size_t n) {
	if (grabit_buf_grow(b, b->len + n + 1) != 0) return -1;
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = '\0';
	return 0;
}

int grabit_buf_puts(struct grabit_buf *b, const char *s) {
	return grabit_buf_putn(b, s, strlen(s));
}

int grabit_buf_putc(struct grabit_buf *b, char c) {
	return grabit_buf_putn(b, &c, 1);
}

void grabit_buf_free(struct grabit_buf *b) {
	free(b->data);
	b->data = NULL;
	b->len = b->cap = 0;
}

int grabit_runtime_dir(char *out, size_t cap) {
	const char *xdg = getenv("XDG_RUNTIME_DIR");
	if (xdg && xdg[0] == '/') {
		struct stat s;
		if (stat(xdg, &s) == 0 && S_ISDIR(s.st_mode)) {
			int n = snprintf(out, cap, "%s", xdg);
			return (n > 0 && (size_t)n < cap) ? 0 : -1;
		}
	}
	int n = snprintf(out, cap, "/tmp");
	return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

bool grabit_process_alive(pid_t pid) {
	if (pid <= 0) return false;
	if (kill(pid, 0) == 0) return true;
	return errno != ESRCH;
}

int grabit_shm_argb_buf(struct wl_shm *shm, const char *tag,
						int32_t pixel_w, int32_t pixel_h,
						struct grabit_shm_buf *out) {
	memset(out, 0, sizeof *out);
	if (pixel_w <= 0 || pixel_h <= 0 ||
		pixel_w > GRABIT_MAX_PIXEL_SIDE || pixel_h > GRABIT_MAX_PIXEL_SIDE) {
		log_error("shm buffer %dx%d out of range", pixel_w, pixel_h);
		return -1;
	}
	int32_t stride = pixel_w * 4;
	size_t size = (size_t)stride * (size_t)pixel_h;
	int fd = grabit_shm_anon(tag ? tag : "grabit", size);
	if (fd < 0) return -1;
	void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		log_error("mmap(%zu): %s", size, strerror(errno));
		close(fd);
		return -1;
	}
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
	struct wl_buffer *buf = wl_shm_pool_create_buffer(pool, 0, pixel_w, pixel_h,
													  stride, WL_SHM_FORMAT_ARGB8888);
	wl_shm_pool_destroy(pool);
	close(fd);
	out->buffer = buf;
	out->map = map;
	out->size = size;
	return 0;
}

void grabit_shm_buf_destroy(struct grabit_shm_buf *b) {
	if (!b) return;
	if (b->buffer) wl_buffer_destroy(b->buffer);
	if (b->map) munmap(b->map, b->size);
	memset(b, 0, sizeof *b);
}

int grabit_waitpid_intr(pid_t pid, int *status) {
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) continue;
		return -1;
	}
	return 0;
}

bool grabit_is_grabit_process(pid_t pid) {
	if (pid <= 0) return false;
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/comm", (int)pid);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char comm[32] = {0};
	bool ok = fgets(comm, sizeof comm, f) != NULL;
	fclose(f);
	if (!ok) return false;
	char *nl = strchr(comm, '\n');
	if (nl) *nl = '\0';
	return strcmp(comm, "grabit") == 0;
}
