// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <leptonica/allheaders.h>
#include <tesseract/capi.h>

struct grabit_ocr {
	TessBaseAPI *api;
};

static int silence_stderr_begin(void) {
	int saved = dup(STDERR_FILENO);
	if (saved < 0) return -1;
	int devnull = open("/dev/null", O_WRONLY);
	if (devnull < 0) {
		close(saved);
		return -1;
	}
	if (dup2(devnull, STDERR_FILENO) < 0) {
		close(devnull);
		close(saved);
		return -1;
	}
	close(devnull);
	return saved;
}

static void silence_stderr_end(int saved) {
	if (saved < 0) return;
	dup2(saved, STDERR_FILENO);
	close(saved);
}

struct grabit_ocr *grabit_ocr_open(void) {
	TessBaseAPI *api = TessBaseAPICreate();
	if (!api) {
		log_error("ocr: TessBaseAPICreate failed");
		return NULL;
	}

	int saved = silence_stderr_begin();
	int rc = TessBaseAPIInit3(api, NULL, "eng");
	silence_stderr_end(saved);

	if (rc != 0) {
		log_error("ocr: tesseract could not load language 'eng'");
		log_error("  install the english training data:");
		log_error("    void:   xbps-install -S tesseract-ocr-eng");
		log_error("    arch:   pacman -S tesseract-data-eng");
		log_error("    debian: apt install tesseract-ocr-eng");
		log_error("    fedora: dnf install tesseract-langpack-eng");
		log_error("  or download manually:");
		log_error("    sudo curl -L -o /usr/share/tessdata/eng.traineddata \\");
		log_error("      https://github.com/tesseract-ocr/tessdata_fast/raw/main/eng.traineddata");
		log_error("  or point TESSDATA_PREFIX at an existing tessdata dir.");
		TessBaseAPIDelete(api);
		return NULL;
	}

	struct grabit_ocr *o = calloc(1, sizeof *o);
	if (!o) {
		log_error("ocr: oom");
		TessBaseAPIEnd(api);
		TessBaseAPIDelete(api);
		return NULL;
	}
	o->api = api;
	return o;
}

char *grabit_ocr_image(struct grabit_ocr *o, const char *path) {
	if (!o || !o->api || !path || !path[0]) return NULL;

	int saved = silence_stderr_begin();
	PIX *pix = pixRead(path);
	silence_stderr_end(saved);
	if (!pix) {
		log_error("ocr: could not load image %s", path);
		return NULL;
	}
	TessBaseAPISetImage2(o->api, pix);

	saved = silence_stderr_begin();
	char *raw = TessBaseAPIGetUTF8Text(o->api);
	silence_stderr_end(saved);

	char *out = NULL;
	if (raw) {
		size_t n = strlen(raw);
		while (n > 0 && (raw[n - 1] == '\n' || raw[n - 1] == '\r' ||
						 raw[n - 1] == ' ' || raw[n - 1] == '\t')) {
			n--;
		}
		out = malloc(n + 1);
		if (out) {
			memcpy(out, raw, n);
			out[n] = '\0';
		} else {
			log_error("ocr: oom copying result");
		}
	}
	TessDeleteText(raw);
	pixDestroy(&pix);
	return out;
}

void grabit_ocr_close(struct grabit_ocr *o) {
	if (!o) return;
	if (o->api) {
		TessBaseAPIEnd(o->api);
		TessBaseAPIDelete(o->api);
	}
	free(o);
}
