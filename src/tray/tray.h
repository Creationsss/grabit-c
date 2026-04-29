// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_H
#define GRABIT_TRAY_H

struct tray_state;

struct tray_state *tray_start(void);
void tray_stop(struct tray_state *t);

#endif
