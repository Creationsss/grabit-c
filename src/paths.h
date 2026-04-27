// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PATHS_H
#define GRABIT_PATHS_H

#include <stddef.h>

const char *paths_config_dir(void);
const char *paths_config_file(void);

int paths_mkdir_p(const char *path);

int paths_atomic_write(const char *path, const void *buf, size_t len);

#endif
