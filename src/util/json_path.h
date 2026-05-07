// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_JSON_PATH_H
#define GRABIT_JSON_PATH_H

struct json_object;

char *grabit_json_path_string(struct json_object *root, const char *path);

char *grabit_json_get_string(struct json_object *obj, const char *key);

#endif
