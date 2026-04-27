// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _GNU_SOURCE
#include "util.h"

#include "log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
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
	while (cap < need) cap *= 2;
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
