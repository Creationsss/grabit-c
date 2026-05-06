// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/sxcu_request.h"

#include "upload/sxcu.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

static int append_encoded_pair(CURL *c, struct grabit_buf *b, char sep,
							   const char *k, const char *v_raw) {
	char *enc_k = curl_easy_escape(c, k, 0);
	char *enc_v = curl_easy_escape(c, v_raw ? v_raw : "", 0);
	int rc = 0;
	if (sep && grabit_buf_putc(b, sep) != 0)
		rc = -1;
	else if (grabit_buf_puts(b, enc_k ? enc_k : k) != 0)
		rc = -1;
	else if (grabit_buf_putc(b, '=') != 0)
		rc = -1;
	else if (grabit_buf_puts(b, enc_v ? enc_v : "") != 0)
		rc = -1;
	curl_free(enc_k);
	curl_free(enc_v);
	return rc;
}

char *sxcu_build_url(CURL *c, const struct sxcu_uploader *u, const char *file_path) {
	char *base = sxcu_expand_input(u->request_url, file_path);
	if (!base) return NULL;
	if (u->n_params == 0) return base;

	struct grabit_buf b = {0};
	if (grabit_buf_puts(&b, base) != 0) {
		free(base);
		grabit_buf_free(&b);
		return NULL;
	}
	free(base);

	char sep = strchr((char *)b.data, '?') ? '&' : '?';
	for (size_t i = 0; i < u->n_params; i++) {
		char *v = sxcu_expand_input(u->params[i].v, file_path);
		int rc = append_encoded_pair(c, &b, sep, u->params[i].k, v);
		free(v);
		if (rc != 0) {
			grabit_buf_free(&b);
			return NULL;
		}
		sep = '&';
	}
	if (grabit_buf_putc(&b, '\0') != 0) {
		grabit_buf_free(&b);
		return NULL;
	}
	return b.data;
}

struct curl_slist *sxcu_build_headers(const struct sxcu_uploader *u, const char *file_path,
									  const char *content_type) {
	struct curl_slist *list = NULL;
	for (size_t i = 0; i < u->n_headers; i++) {
		char *v = sxcu_expand_input(u->headers[i].v, file_path);
		char *line = NULL;
		grabit_xasprintf(&line, "%s: %s", u->headers[i].k, v ? v : "");
		if (line) list = curl_slist_append(list, line);
		free(v);
		free(line);
	}
	if (content_type) {
		char *line = NULL;
		grabit_xasprintf(&line, "Content-Type: %s", content_type);
		if (line) list = curl_slist_append(list, line);
		free(line);
	}
	return list;
}

curl_mime *sxcu_build_multipart(CURL *c, const struct sxcu_uploader *u, const char *file_path) {
	curl_mime *mime = curl_mime_init(c);
	if (!mime) return NULL;
	for (size_t i = 0; i < u->n_args; i++) {
		curl_mimepart *part = curl_mime_addpart(mime);
		curl_mime_name(part, u->args[i].k);
		char *v = sxcu_expand_input(u->args[i].v, file_path);
		curl_mime_data(part, v ? v : "", CURL_ZERO_TERMINATED);
		free(v);
	}
	if (u->file_form_name && file_path) {
		curl_mimepart *part = curl_mime_addpart(mime);
		curl_mime_name(part, u->file_form_name);
		curl_mime_filedata(part, file_path);
	}
	return mime;
}

char *sxcu_build_form_url(CURL *c, const struct sxcu_uploader *u, const char *file_path) {
	struct grabit_buf b = {0};
	for (size_t i = 0; i < u->n_args; i++) {
		char *v = sxcu_expand_input(u->args[i].v, file_path);
		int rc = append_encoded_pair(c, &b, i == 0 ? 0 : '&', u->args[i].k, v);
		free(v);
		if (rc != 0) goto fail;
	}
	if (grabit_buf_putc(&b, '\0') != 0) goto fail;
	return b.data;
fail:
	grabit_buf_free(&b);
	return NULL;
}

char *sxcu_build_json_body(const struct sxcu_uploader *u, const char *file_path) {
	if (u->data) return sxcu_expand_input(u->data, file_path);
	struct json_object *o = json_object_new_object();
	if (!o) return NULL;
	for (size_t i = 0; i < u->n_args; i++) {
		char *v = sxcu_expand_input(u->args[i].v, file_path);
		json_object_object_add(o, u->args[i].k, json_object_new_string(v ? v : ""));
		free(v);
	}
	const char *s = json_object_to_json_string(o);
	char *out = s ? strdup(s) : NULL;
	json_object_put(o);
	return out;
}

int sxcu_read_binary_body(const char *file_path, char **out, long *out_len) {
	size_t sz = 0;
	if (grabit_read_file(file_path, 0, out, &sz) != 0) return -1;
	*out_len = (long)sz;
	return 0;
}
