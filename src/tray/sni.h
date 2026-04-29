// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_TRAY_SNI_H
#define GRABIT_TRAY_SNI_H

#include <signal.h>

int sni_run(volatile sig_atomic_t *stop);

#endif
