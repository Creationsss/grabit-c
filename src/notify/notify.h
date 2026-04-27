// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_NOTIFY_H
#define GRABIT_NOTIFY_H

#include <stdbool.h>

struct config;

void notify_init(struct config *cfg);

struct notify_opts {
	const char *summary;
	const char *body;
	const char *icon_path;
	bool        force;
};

void notify_send(const struct notify_opts *o);

#endif
