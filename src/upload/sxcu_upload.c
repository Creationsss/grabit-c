// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/sxcu.h"

#include "log.h"
#include "mime.h"
#include "upload/sxcu_request.h"
#include "upload/upload.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#ifndef GRABIT_VERSION
#define GRABIT_VERSION "0.0.0"
#endif

struct write_ctx {
	struct grabit_buf body;
	struct sxcu_kv *headers;
	size_t n_headers;
	size_t cap_headers;
};

static size_t on_body(void *ptr, size_t sz, size_t nm, void *ud) {
	struct write_ctx *w = ud;
	size_t total = sz * nm;
	return grabit_buf_putn(&w->body, ptr, total) == 0 ? total : 0;
}

static size_t on_header(char *buf, size_t sz, size_t nm, void *ud) {
	struct write_ctx *w = ud;
	size_t total = sz * nm;
	const char *colon = memchr(buf, ':', total);
	if (!colon) return total;
	size_t name_len = (size_t)(colon - buf);
	const char *val = colon + 1;
	size_t val_len = total - name_len - 1;
	while (val_len > 0 && (*val == ' ' || *val == '\t')) {
		val++;
		val_len--;
	}
	while (val_len > 0 && (val[val_len - 1] == '\r' || val[val_len - 1] == '\n')) {
		val_len--;
	}
	if (w->n_headers == w->cap_headers) {
		size_t cap = w->cap_headers ? w->cap_headers * 2 : 8;
		struct sxcu_kv *p = realloc(w->headers, cap * sizeof *p);
		if (!p) {
			log_warn("sxcu: oom growing response headers; dropping rest");
			return total;
		}
		w->headers = p;
		w->cap_headers = cap;
	}
	char *k = strndup(buf, name_len);
	char *v = strndup(val, val_len);
	if (k && v) {
		w->headers[w->n_headers].k = k;
		w->headers[w->n_headers].v = v;
		w->n_headers++;
	} else {
		free(k);
		free(v);
	}
	return total;
}

static void apply_method(CURL *c, enum sxcu_method m) {
	switch (m) {
	case SXCU_GET:
		break;
	case SXCU_POST:
		curl_easy_setopt(c, CURLOPT_POST, 1L);
		break;
	case SXCU_PUT:
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
		break;
	case SXCU_PATCH:
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PATCH");
		break;
	case SXCU_DELETE:
		curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
		break;
	}
}

static char *trim_right(char *s) {
	if (!s) return s;
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) {
		s[--n] = '\0';
	}
	return s;
}

static int build_body(CURL *c, const struct sxcu_uploader *u, const char *file_path,
					  curl_mime **mime_out, char **body_out, long *body_len_out,
					  const char **forced_ct_out, char **binary_ct_out) {
	*mime_out = NULL;
	*body_out = NULL;
	*body_len_out = 0;
	*forced_ct_out = NULL;
	*binary_ct_out = NULL;

	if (u->body_type == SXCU_BODY_NONE) return 0;

	if (u->method == SXCU_GET) {
		log_error("sxcu: GET with Body=%s not supported", sxcu_body_str(u->body_type));
		return -1;
	}

	switch (u->body_type) {
	case SXCU_BODY_NONE:
		return 0;
	case SXCU_BODY_MULTIPART:
		*mime_out = sxcu_build_multipart(c, u, file_path);
		if (!*mime_out) return -1;
		curl_easy_setopt(c, CURLOPT_MIMEPOST, *mime_out);
		return 0;
	case SXCU_BODY_FORM_URL:
		*body_out = sxcu_build_form_url(c, u, file_path);
		if (!*body_out) return -1;
		*body_len_out = (long)strlen(*body_out);
		*forced_ct_out = "application/x-www-form-urlencoded";
		break;
	case SXCU_BODY_JSON:
		*body_out = sxcu_build_json_body(u, file_path);
		if (!*body_out) return -1;
		*body_len_out = (long)strlen(*body_out);
		*forced_ct_out = "application/json";
		break;
	case SXCU_BODY_XML:
		*body_out = u->data ? sxcu_expand_input(u->data, file_path) : strdup("");
		if (!*body_out) return -1;
		*body_len_out = (long)strlen(*body_out);
		*forced_ct_out = "application/xml";
		break;
	case SXCU_BODY_BINARY:
		if (sxcu_read_binary_body(file_path, body_out, body_len_out) != 0) return -1;
		*binary_ct_out = mime_for_file(file_path);
		*forced_ct_out = *binary_ct_out ? *binary_ct_out : "application/octet-stream";
		break;
	}
	curl_easy_setopt(c, CURLOPT_POSTFIELDS, *body_out);
	curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, *body_len_out);
	return 0;
}

int sxcu_upload(const struct sxcu_uploader *u, const char *file_path,
				struct upload_result *result) {
	if (!u || !file_path || !result) return -1;
	memset(result, 0, sizeof *result);

	CURL *c = curl_easy_init();
	if (!c) return -1;

	char *url = sxcu_build_url(c, u, file_path);
	if (!url) {
		curl_easy_cleanup(c);
		return -1;
	}
	curl_easy_setopt(c, CURLOPT_URL, url);
	apply_method(c, u->method);

	curl_mime *mime = NULL;
	char *body_str = NULL;
	long body_len = 0;
	const char *forced_ct = NULL;
	char *binary_ct = NULL;
	struct curl_slist *hdrs = NULL;
	struct write_ctx w = {0};
	int ret = -1;

	if (build_body(c, u, file_path, &mime, &body_str, &body_len, &forced_ct, &binary_ct) != 0) {
		goto cleanup;
	}

	hdrs = sxcu_build_headers(u, file_path, forced_ct);
	if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, &w);
	curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
	curl_easy_setopt(c, CURLOPT_HEADERDATA, &w);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_USERAGENT, "grabit/" GRABIT_VERSION);

	CURLcode rc = curl_easy_perform(c);
	long status = 0;
	char *eff_url = NULL;
	curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
	curl_easy_getinfo(c, CURLINFO_EFFECTIVE_URL, &eff_url);

	const char *body_data = w.body.data ? w.body.data : "";
	result->http_code = status;

	if (rc == CURLE_OK && status >= 200 && status < 300) {
		char *out_url = u->url_expr
							? sxcu_expand_response(u->url_expr, body_data,
												   w.headers, w.n_headers, eff_url,
												   u->regex_list, u->n_regex_list)
							: strdup(body_data);
		out_url = trim_right(out_url);
		if (!out_url || !*out_url) {
			free(out_url);
			out_url = trim_right(strdup(body_data));
		}
		result->url = out_url;
		ret = 0;
	} else {
		char *err = u->err_expr
						? sxcu_expand_response(u->err_expr, body_data,
											   w.headers, w.n_headers, eff_url,
											   u->regex_list, u->n_regex_list)
						: NULL;
		result->body = err ? err : strdup(body_data);
		log_error("sxcu: upload failed (curl=%s, http=%ld)",
				  curl_easy_strerror(rc), status);
	}

cleanup:
	if (mime) curl_mime_free(mime);
	if (hdrs) curl_slist_free_all(hdrs);
	free(body_str);
	free(binary_ct);
	free(url);
	grabit_buf_free(&w.body);
	sxcu_kv_free(w.headers, w.n_headers);
	curl_easy_cleanup(c);
	return ret;
}
