// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_REGION_ANNOTATE_H
#define GRABIT_REGION_ANNOTATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <cairo/cairo.h>

struct annotation;
struct annotation_list;

void annotation_paint(cairo_t *cr, const struct annotation *a, double scale);
void annotation_list_paint(cairo_t *cr, const struct annotation_list *list,
						   int32_t origin_x, int32_t origin_y, double scale);

int annotation_list_push(struct annotation_list *list, const struct annotation *a);
void annotation_list_pop(struct annotation_list *list);
void annotation_free(struct annotation *a);

#endif
