// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_SDBUS_H
#define GRABIT_SDBUS_H

#if defined(GRABIT_BUS_BASU)
#  include <basu/sd-bus.h>
#elif defined(GRABIT_BUS_ELOGIND)
#  include <elogind/sd-bus.h>
#else
#  include <systemd/sd-bus.h>
#endif

#endif
