// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PIN_H
#define GRABIT_PIN_H

struct config;
struct rect;

int pin_spawn(struct config *cfg, const char *path, const struct rect *r);
int pin_grab(void);
int pin_release(void);
int pin_close_all(void);

#endif
