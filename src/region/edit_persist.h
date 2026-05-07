// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_EDIT_PERSIST_H
#define GRABIT_REGION_EDIT_PERSIST_H

#include <stdint.h>
#include <stddef.h>

struct config;

uint32_t edit_color_from_str(const char *s);
void edit_color_to_str(uint32_t hex, char *buf, size_t cap);
int32_t edit_width_from_str(const char *s);
void persist_edit_choices(struct config *cfg, uint32_t color, int32_t width);

#endif
