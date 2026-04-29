// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_H
#define GRABIT_UPLOAD_H

#include <stdbool.h>

struct config;
struct args;

bool upload_service_known(const char *name);

int upload_preflight(struct config *cfg, const struct args *a, const char **service_out);

struct upload_result {
	long http_code;
	char *url;
	char *body;
};

void upload_result_free(struct upload_result *r);

int upload_perform(const char *service_name,
				   const char *file_path,
				   struct config *cfg,
				   struct upload_result *out);

#endif
