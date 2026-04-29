// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UTIL_H
#define GRABIT_UTIL_H

#include <stdbool.h>
#include <stddef.h>

#define GRABIT_MAX_PIXEL_SIDE 16384

int grabit_xasprintf(char **out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int grabit_shm_anon(const char *tag, size_t size);

bool grabit_in_path(const char *bin);

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

#endif
