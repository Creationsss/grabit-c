// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_FETCH_H
#define GRABIT_PLUGIN_FETCH_H

#include <stdbool.h>
#include <time.h>

enum plugin_fetch_result {
	PLUGIN_FETCH_OK,
	PLUGIN_FETCH_NOT_MODIFIED,
	PLUGIN_FETCH_FAIL,
};

enum plugin_fetch_result plugin_fetch_url(const char *url, const char *dst,
										  time_t if_modified_since);

int plugin_sha256_file(const char *path, char *hex_out);

bool plugin_sha256_equal(const char *expect_hex, const char *actual_hex);

#endif
