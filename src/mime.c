// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700

#include "mime.h"

#include "log.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <magic.h>

static char *fallback(void) {
	return strdup("application/octet-stream");
}

char *mime_for_file(const char *path) {
	if (!path) return fallback();

	magic_t m = magic_open(MAGIC_MIME_TYPE);
	if (!m) {
		log_debug("magic_open failed");
		return fallback();
	}
	if (magic_load(m, NULL) != 0) {
		log_debug("magic_load: %s", magic_error(m));
		magic_close(m);
		return fallback();
	}

	const char *raw = magic_file(m, path);
	char *out;
	if (!raw) {
		log_debug("magic_file(%s): %s", path, magic_error(m));
		out = fallback();
	} else {
		out = strdup(raw);
		if (out) {
			for (char *p = out; *p; p++) *p = (char)tolower((unsigned char)*p);
		} else {
			out = fallback();
		}
	}
	magic_close(m);
	return out;
}

bool mime_is_image(const char *m) {
	return m && strncmp(m, "image/", 6) == 0;
}

bool mime_is_video(const char *m) {
	return m && strncmp(m, "video/", 6) == 0;
}
