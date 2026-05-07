// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_STATE_H
#define GRABIT_PLUGIN_STATE_H

#include <stddef.h>

int plugin_state_write(const char *plugin_dir, const char *kind,
					   const char *url, const char *sha256);
int plugin_state_read(const char *plugin_dir,
					  char *kind, size_t kind_cap,
					  char *url, size_t url_cap,
					  char *sha, size_t sha_cap);

#endif
