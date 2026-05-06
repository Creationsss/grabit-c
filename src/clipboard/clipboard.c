// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard.h"

#include "clipboard/clipboard_internal.h"
#include "log.h"
#include "util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define GRABIT_CLIP_MAX_FILE_SIZE ((size_t)100 * 1024 * 1024)

int clipboard_set_text(const char *text) {
	if (!text) return -1;
	static const char *const TEXT_MIMES[] = {
		"text/plain;charset=utf-8",
		"text/plain",
		"UTF8_STRING",
		"STRING",
		"TEXT",
	};
	return clipboard_send_bytes(text, strlen(text),
								TEXT_MIMES,
								sizeof TEXT_MIMES / sizeof TEXT_MIMES[0]);
}

int clipboard_set_image_file(const char *path) {
	if (!path) return -1;

	char *buf = NULL;
	size_t sz = 0;
	if (grabit_read_file(path, GRABIT_CLIP_MAX_FILE_SIZE, &buf, &sz) != 0) {
		if (errno == EFBIG) {
			log_error("clipboard: file %s exceeds %zu-byte cap", path,
					  GRABIT_CLIP_MAX_FILE_SIZE);
		} else {
			log_error("clipboard: read %s: %s", path, strerror(errno));
		}
		return -1;
	}

	static const char *const IMAGE_MIMES[] = {
		"image/png",
	};
	int rc = clipboard_send_bytes(buf, sz,
								  IMAGE_MIMES,
								  sizeof IMAGE_MIMES / sizeof IMAGE_MIMES[0]);

	free(buf);
	return rc;
}
