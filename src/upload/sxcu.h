// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_UPLOAD_SXCU_H
#define GRABIT_UPLOAD_SXCU_H

#include <stdbool.h>
#include <stddef.h>

struct upload_result;

enum sxcu_method {
	SXCU_GET,
	SXCU_POST,
	SXCU_PUT,
	SXCU_PATCH,
	SXCU_DELETE,
};

enum sxcu_body_type {
	SXCU_BODY_NONE,
	SXCU_BODY_MULTIPART,
	SXCU_BODY_FORM_URL,
	SXCU_BODY_JSON,
	SXCU_BODY_XML,
	SXCU_BODY_BINARY,
};

struct sxcu_kv {
	char *k;
	char *v;
};

void sxcu_kv_free(struct sxcu_kv *arr, size_t n);

struct sxcu_uploader {
	char *name;
	char *request_url;
	enum sxcu_method method;
	enum sxcu_body_type body_type;
	char *file_form_name;
	char *data;
	char *url_expr;
	char *thumb_expr;
	char *del_expr;
	char *err_expr;
	struct sxcu_kv *params;
	size_t n_params;
	struct sxcu_kv *headers;
	size_t n_headers;
	struct sxcu_kv *args;
	size_t n_args;
	char **regex_list;
	size_t n_regex_list;
	bool is_image_uploader;
};

int sxcu_parse_file(const char *path, struct sxcu_uploader *out);
int sxcu_parse_string(const char *json, struct sxcu_uploader *out);
void sxcu_free(struct sxcu_uploader *u);

const char *sxcu_method_str(enum sxcu_method m);
const char *sxcu_body_str(enum sxcu_body_type t);

int sxcu_upload(const struct sxcu_uploader *u, const char *file_path,
				struct upload_result *result);

const char *sxcu_dir_path(void);
int sxcu_dir_list(char ***names_out, size_t *n_out);
int sxcu_dir_add(const char *file_path);
int sxcu_dir_remove(const char *name);
int sxcu_dir_lookup(const char *name, struct sxcu_uploader *out);
bool sxcu_dir_has(const char *name);

char *sxcu_expand_input(const char *tmpl, const char *file_path);
/* body must be NUL-terminated */
char *sxcu_expand_response(const char *tmpl, const char *body,
						   const struct sxcu_kv *resp_headers, size_t n_resp_headers,
						   const char *response_url,
						   char *const *regex_list, size_t n_regex_list);

#endif
