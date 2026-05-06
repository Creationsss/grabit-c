// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_SXCU_REQUEST_H
#define GRABIT_UPLOAD_SXCU_REQUEST_H

#include <curl/curl.h>

struct sxcu_uploader;

char *sxcu_build_url(CURL *c, const struct sxcu_uploader *u, const char *file_path);
struct curl_slist *sxcu_build_headers(const struct sxcu_uploader *u, const char *file_path,
									  const char *content_type);
curl_mime *sxcu_build_multipart(CURL *c, const struct sxcu_uploader *u, const char *file_path);
char *sxcu_build_form_url(CURL *c, const struct sxcu_uploader *u, const char *file_path);
char *sxcu_build_json_body(const struct sxcu_uploader *u, const char *file_path);
int sxcu_read_binary_body(const char *file_path, char **out, long *out_len);

#endif
