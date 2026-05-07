// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_PLUGIN_DISPATCH_H
#define GRABIT_PLUGIN_DISPATCH_H

void plugin_dispatch_set_env(const char *name);

int plugin_dispatch_pin(const char *name, int argc, char **argv);

#endif
