// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_MIME_H
#define GRABIT_MIME_H

#include <stdbool.h>

char *mime_for_file(const char *path);

bool mime_is_image(const char *m);
bool mime_is_video(const char *m);

#endif
