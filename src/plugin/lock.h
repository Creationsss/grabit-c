// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_LOCK_H
#define GRABIT_PLUGIN_LOCK_H

int plugin_lock_acquire(void);
void plugin_lock_release(int fd);

#endif
