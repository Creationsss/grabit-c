// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CLIPBOARD_INTERNAL_H
#define GRABIT_CLIPBOARD_INTERNAL_H

#include <stddef.h>

int clipboard_send_bytes(const void *bytes, size_t size,
						 const char *const *mimes, size_t n_mimes);

#endif
