// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UTIL_H
#define GRABIT_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define GRABIT_MAX_PIXEL_SIDE 16384

int grabit_xasprintf(char **out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int grabit_shm_anon(const char *tag, size_t size);

bool grabit_in_path(const char *bin);

const char *grabit_basename(const char *path);

int grabit_runtime_dir(char *out, size_t cap);
bool grabit_process_alive(pid_t pid);
bool grabit_is_grabit_process(pid_t pid);

int grabit_waitpid_intr(pid_t pid, int *status);

struct wl_shm;
struct wl_buffer;
struct grabit_shm_buf {
	struct wl_buffer *buffer;
	void *map;
	size_t size;
};
int grabit_shm_argb_buf(struct wl_shm *shm, const char *tag,
						int32_t pixel_w, int32_t pixel_h,
						struct grabit_shm_buf *out);
void grabit_shm_buf_destroy(struct grabit_shm_buf *b);
void grabit_shm_release(struct wl_buffer **buf, void **map, size_t *size);

void grabit_redirect_stdio_devnull(void);

struct grabit_buf {
	char *data;
	size_t len;
	size_t cap;
};

int grabit_buf_grow(struct grabit_buf *b, size_t need);
int grabit_buf_putn(struct grabit_buf *b, const void *s, size_t n);
int grabit_buf_puts(struct grabit_buf *b, const char *s);
int grabit_buf_putc(struct grabit_buf *b, char c);
void grabit_buf_free(struct grabit_buf *b);

int grabit_read_file(const char *path, size_t max_bytes, char **out, size_t *out_len);

#endif
