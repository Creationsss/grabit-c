// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "template.h"

#include "log.h"
#include "util.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int read_random(unsigned char *out, size_t n) {
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	size_t off = 0;
	while (off < n) {
		ssize_t r = read(fd, out + off, n - off);
		if (r <= 0) {
			close(fd);
			return -1;
		}
		off += (size_t)r;
	}
	close(fd);
	return 0;
}

static const char ALNUM[] =
	"abcdefghijklmnopqrstuvwxyz"
	"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	"0123456789";

static int put_random_alnum(struct grabit_buf *b, unsigned len) {
	if (len == 0) return 0;
	unsigned char raw[256];
	if (len > sizeof raw) len = sizeof raw;
	if (read_random(raw, len) != 0) {
		log_error("read /dev/urandom for %%r token failed");
		return -1;
	}
	for (unsigned i = 0; i < len; i++) {
		if (grabit_buf_putc(b, ALNUM[raw[i] % (sizeof ALNUM - 1)]) != 0) return -1;
	}
	return 0;
}

static int put_uuid_v4(struct grabit_buf *b) {
	unsigned char r[16];
	if (read_random(r, sizeof r) != 0) {
		log_error("read /dev/urandom for %%u token failed");
		return -1;
	}
	r[6] = (unsigned char)((r[6] & 0x0F) | 0x40);
	r[8] = (unsigned char)((r[8] & 0x3F) | 0x80);
	char out[37];
	snprintf(out, sizeof out,
	         "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
	         r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
	         r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
	return grabit_buf_puts(b, out);
}

char *template_expand(const char *tpl, const struct template_ctx *ctx) {
	if (!tpl) return strdup("");

	struct grabit_buf b = {0};
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);

	for (const char *p = tpl; *p; p++) {
		if (*p != '%') {
			if (grabit_buf_putc(&b, *p) != 0) goto oom;
			continue;
		}

		char kind = *++p;
		if (kind == '\0') {
			if (grabit_buf_putc(&b, '%') != 0) goto oom;
			break;
		}

		char tmp[64];
		switch (kind) {
		case '%':
			if (grabit_buf_putc(&b, '%') != 0) goto oom;
			break;
		case 'Y':
			snprintf(tmp, sizeof tmp, "%04d", tm.tm_year + 1900);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'm':
			snprintf(tmp, sizeof tmp, "%02d", tm.tm_mon + 1);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'd':
			snprintf(tmp, sizeof tmp, "%02d", tm.tm_mday);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'H':
			snprintf(tmp, sizeof tmp, "%02d", tm.tm_hour);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'M':
			snprintf(tmp, sizeof tmp, "%02d", tm.tm_min);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'S':
			snprintf(tmp, sizeof tmp, "%02d", tm.tm_sec);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 's':
			snprintf(tmp, sizeof tmp, "%lld", (long long)now);
			if (grabit_buf_puts(&b, tmp) != 0) goto oom;
			break;
		case 'u':
			if (put_uuid_v4(&b) != 0) goto oom;
			break;
		case 'r': {
			unsigned len = 0;
			while (isdigit((unsigned char)p[1])) {
				len = len * 10 + (unsigned)(*++p - '0');
				if (len > 256) len = 256;
			}
			if (len == 0) len = 12;
			if (put_random_alnum(&b, len) != 0) goto oom;
			break;
		}
		case 'w':
			if (ctx && ctx->window_class && grabit_buf_puts(&b, ctx->window_class) != 0) goto oom;
			break;
		case 't':
			if (ctx && ctx->window_title && grabit_buf_puts(&b, ctx->window_title) != 0) goto oom;
			break;
		case 'o':
			if (ctx && ctx->output_name && grabit_buf_puts(&b, ctx->output_name) != 0) goto oom;
			break;
		default:
			if (grabit_buf_putc(&b, '%') != 0) goto oom;
			if (grabit_buf_putc(&b, kind) != 0) goto oom;
			break;
		}
	}

	return b.data ? b.data : strdup("");

oom:
	free(b.data);
	return NULL;
}

static int filename_safe(unsigned char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

char *template_sanitize(const char *s) {
	if (!s) return strdup("");
	struct grabit_buf b = {0};
	bool prev_underscore = false;
	for (const char *p = s; *p; p++) {
		if (filename_safe((unsigned char)*p)) {
			if (grabit_buf_putc(&b, *p) != 0) {
				free(b.data);
				return NULL;
			}
			prev_underscore = false;
		} else {
			if (!prev_underscore) {
				if (grabit_buf_putc(&b, '_') != 0) {
					free(b.data);
					return NULL;
				}
				prev_underscore = true;
			}
		}
	}
	if (!b.data) return strdup("");

	size_t start = 0;
	while (start < b.len && b.data[start] == '_') start++;
	if (start > 0) {
		memmove(b.data, b.data + start, b.len - start + 1);
		b.len -= start;
	}
	while (b.len > 0 && b.data[b.len - 1] == '_') {
		b.data[--b.len] = '\0';
	}
	return b.data;
}

const char *template_for_preset(const char *preset) {
	if (!preset) return "%Y-%m-%d-%H-%M-%S";
	if (strcmp(preset, "random") == 0) return "%r12";
	if (strcmp(preset, "uuid") == 0) return "%u";
	if (strcmp(preset, "timestamp") == 0) return "%s";
	return "%Y-%m-%d-%H-%M-%S";
}
