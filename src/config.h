// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_CONFIG_H
#define GRABIT_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

struct kv {
	char *key;
	char *val;
};

struct config {
	struct kv *kvs;
	size_t     n;
	size_t     cap;
};

int  config_load(struct config *c);
int  config_save(struct config *c);
void config_free(struct config *c);

const char *config_get(struct config *c, const char *key);
int         config_set(struct config *c, const char *key, const char *value);
bool        config_needs_setup(struct config *c);

int cmd_set(int argc, char **argv);
int cmd_get(int argc, char **argv);

#endif
