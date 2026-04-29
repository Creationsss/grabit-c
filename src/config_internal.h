// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CONFIG_INTERNAL_H
#define GRABIT_CONFIG_INTERNAL_H

#include <stddef.h>

enum zl_kind { ZL_FREE,
			   ZL_ENUM,
			   ZL_INT,
			   ZL_INT_PCT };

struct zl_hdr {
	const char *name;
	enum zl_kind kind;
	const char **allowed;
};

extern const struct zl_hdr gcfg_zl_headers[];
extern const size_t gcfg_zl_headers_n;

const struct zl_hdr *gcfg_zl_find(const char *name);
int gcfg_cmp_kv(const void *a, const void *b);

#endif
