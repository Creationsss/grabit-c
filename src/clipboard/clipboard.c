// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "clipboard/clipboard.h"

#include "clipboard/clipboard_internal.h"
#include "log.h"

#include <errno.h>
#include <stdio.h>
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
	int rc = clipboard_send_bytes(buf, (size_t)sz,
								  IMAGE_MIMES,
								  sizeof IMAGE_MIMES / sizeof IMAGE_MIMES[0]);

	free(buf);
	return rc;
}
