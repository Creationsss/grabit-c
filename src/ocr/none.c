// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "ocr/ocr.h"

#include "log.h"

#include <stddef.h>

struct grabit_ocr *grabit_ocr_open(void) {
	log_error("ocr: this grabit was built without tesseract support");
	log_error("  install tesseract + leptonica development headers and rebuild:");
	log_error("    void:   xbps-install -S tesseract-ocr-devel leptonica-devel");
	log_error("    arch:   pacman -S tesseract leptonica");
	log_error("    debian: apt install libtesseract-dev libleptonica-dev");
	log_error("    fedora: dnf install tesseract-devel leptonica-devel");
	return NULL;
}

char *grabit_ocr_image(struct grabit_ocr *o, const char *path) {
	(void)o;
	(void)path;
	return NULL;
}

void grabit_ocr_close(struct grabit_ocr *o) {
	(void)o;
}
