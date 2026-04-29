// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_OCR_H
#define GRABIT_OCR_H

struct grabit_ocr;

struct grabit_ocr *grabit_ocr_open(void);
char *grabit_ocr_image(struct grabit_ocr *o, const char *path);
void grabit_ocr_close(struct grabit_ocr *o);

#endif
