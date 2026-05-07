// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/sxcu.h"

#include "log.h"
#include "util.h"
#include "util/json_path.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <json-c/json.h>

#define SXCU_FILE_MAX (1024 * 1024)
#define UTF8_BOM_0 0xEF
#define UTF8_BOM_1 0xBB
#define UTF8_BOM_2 0xBF
#define UTF8_BOM_LEN 3

void sxcu_kv_free(struct sxcu_kv *arr, size_t n) {
	if (!arr) return;
	for (size_t i = 0; i < n; i++) {
		free(arr[i].k);
		free(arr[i].v);
	}
	free(arr);
}

void sxcu_free(struct sxcu_uploader *u) {
	if (!u) return;
	free(u->name);
	free(u->request_url);
	free(u->file_form_name);
	free(u->data);
	free(u->url_expr);
	free(u->thumb_expr);
	free(u->del_expr);
	free(u->err_expr);
	sxcu_kv_free(u->params, u->n_params);
	sxcu_kv_free(u->headers, u->n_headers);
	sxcu_kv_free(u->args, u->n_args);
	for (size_t i = 0; i < u->n_regex_list; i++)
		free(u->regex_list[i]);
	free(u->regex_list);
	memset(u, 0, sizeof *u);
}

static int populate_kv(struct json_object *obj, const char *key,
					   struct sxcu_kv **out, size_t *n_out) {
	struct json_object *v = NULL;
	if (!json_object_object_get_ex(obj, key, &v)) return 0;
	if (!json_object_is_type(v, json_type_object)) return 0;

	size_t n = 0;
	json_object_object_foreach(v, _k, _val) {
		(void)_k;
		(void)_val;
		n++;
	}
	if (n == 0) return 0;

	struct sxcu_kv *arr = calloc(n, sizeof *arr);
	if (!arr) return -1;

	size_t i = 0;
	json_object_object_foreach(v, kk, val) {
		const char *vs = json_object_get_string(val);
		arr[i].k = strdup(kk);
		arr[i].v = strdup(vs ? vs : "");
		if (!arr[i].k || !arr[i].v) {
			for (size_t j = 0; j <= i; j++) {
				free(arr[j].k);
				free(arr[j].v);
				arr[j].k = arr[j].v = NULL;
			}
			free(arr);
			return -1;
		}
		i++;
	}
	*out = arr;
	*n_out = n;
	return 0;
}

const char *sxcu_method_str(enum sxcu_method m) {
	switch (m) {
	case SXCU_GET:
		return "GET";
	case SXCU_POST:
		return "POST";
	case SXCU_PUT:
		return "PUT";
	case SXCU_PATCH:
		return "PATCH";
	case SXCU_DELETE:
		return "DELETE";
	}
	return "POST";
}

const char *sxcu_body_str(enum sxcu_body_type t) {
	switch (t) {
	case SXCU_BODY_NONE:
		return "None";
	case SXCU_BODY_MULTIPART:
		return "MultipartFormData";
	case SXCU_BODY_FORM_URL:
		return "FormURLEncoded";
	case SXCU_BODY_JSON:
		return "JSON";
	case SXCU_BODY_XML:
		return "XML";
	case SXCU_BODY_BINARY:
		return "Binary";
	}
	return "";
}

static enum sxcu_method parse_method(const char *s) {
	if (!s) return SXCU_POST;
	if (strcasecmp(s, "GET") == 0) return SXCU_GET;
	if (strcasecmp(s, "PUT") == 0) return SXCU_PUT;
	if (strcasecmp(s, "PATCH") == 0) return SXCU_PATCH;
	if (strcasecmp(s, "DELETE") == 0) return SXCU_DELETE;
	return SXCU_POST;
}

static enum sxcu_body_type parse_body(const char *s) {
	if (!s) return SXCU_BODY_MULTIPART;
	if (strcasecmp(s, "None") == 0) return SXCU_BODY_NONE;
	if (strcasecmp(s, "MultipartFormData") == 0) return SXCU_BODY_MULTIPART;
	if (strcasecmp(s, "FormURLEncoded") == 0) return SXCU_BODY_FORM_URL;
	if (strcasecmp(s, "JSON") == 0) return SXCU_BODY_JSON;
	if (strcasecmp(s, "XML") == 0) return SXCU_BODY_XML;
	if (strcasecmp(s, "Binary") == 0) return SXCU_BODY_BINARY;
	return SXCU_BODY_MULTIPART;
}

static int parse_regex_list(struct json_object *root, struct sxcu_uploader *out) {
	struct json_object *rl = NULL;
	if (!json_object_object_get_ex(root, "RegexList", &rl)) return 0;
	if (!json_object_is_type(rl, json_type_array)) return 0;
	size_t n = json_object_array_length(rl);
	if (n == 0) return 0;
	out->regex_list = calloc(n, sizeof *out->regex_list);
	if (!out->regex_list) return -1;
	for (size_t i = 0; i < n; i++) {
		struct json_object *p = json_object_array_get_idx(rl, i);
		const char *s = json_object_get_string(p);
		out->regex_list[i] = strdup(s ? s : "");
		if (!out->regex_list[i]) return -1;
	}
	out->n_regex_list = n;
	return 0;
}

int sxcu_parse_string(const char *json, struct sxcu_uploader *out) {
	if (!json || !out) return -1;
	memset(out, 0, sizeof *out);

	struct json_object *root = json_tokener_parse(json);
	if (!root || !json_object_is_type(root, json_type_object)) {
		log_error("sxcu: not a JSON object");
		if (root) json_object_put(root);
		return -1;
	}

	out->name = grabit_json_get_string(root, "Name");
	out->request_url = grabit_json_get_string(root, "RequestURL");
	if (!out->request_url) {
		log_error("sxcu: missing RequestURL");
		json_object_put(root);
		sxcu_free(out);
		return -1;
	}

	char *method = grabit_json_get_string(root, "RequestMethod");
	if (!method) method = grabit_json_get_string(root, "RequestType");
	out->method = parse_method(method);
	free(method);

	char *body = grabit_json_get_string(root, "Body");
	out->body_type = parse_body(body);
	free(body);

	out->file_form_name = grabit_json_get_string(root, "FileFormName");
	out->data = grabit_json_get_string(root, "Data");
	out->url_expr = grabit_json_get_string(root, "URL");
	out->thumb_expr = grabit_json_get_string(root, "ThumbnailURL");
	out->del_expr = grabit_json_get_string(root, "DeletionURL");
	out->err_expr = grabit_json_get_string(root, "ErrorMessage");

	if (populate_kv(root, "Parameters", &out->params, &out->n_params) != 0) goto fail;
	if (populate_kv(root, "Headers", &out->headers, &out->n_headers) != 0) goto fail;
	if (populate_kv(root, "Arguments", &out->args, &out->n_args) != 0) goto fail;
	if (parse_regex_list(root, out) != 0) goto fail;

	char *dest = grabit_json_get_string(root, "DestinationType");
	out->is_image_uploader = !dest || strstr(dest, "ImageUploader") ||
							 strstr(dest, "FileUploader");
	free(dest);

	json_object_put(root);
	return 0;

fail:
	log_error("sxcu: parse failed (oom?)");
	json_object_put(root);
	sxcu_free(out);
	return -1;
}

int sxcu_parse_file(const char *path, struct sxcu_uploader *out) {
	if (!path) return -1;
	char *buf = NULL;
	size_t sz = 0;
	if (grabit_read_file(path, SXCU_FILE_MAX, &buf, &sz) != 0) {
		log_error("sxcu: read %s: %s", path, strerror(errno));
		return -1;
	}

	const char *json = buf;
	if (sz >= UTF8_BOM_LEN && (uint8_t)buf[0] == UTF8_BOM_0 &&
		(uint8_t)buf[1] == UTF8_BOM_1 && (uint8_t)buf[2] == UTF8_BOM_2) {
		json = buf + UTF8_BOM_LEN;
	}

	int rc = sxcu_parse_string(json, out);
	free(buf);

	if (rc == 0 && !out->name) {
		const char *bn = strrchr(path, '/');
		bn = bn ? bn + 1 : path;
		const char *dot = strrchr(bn, '.');
		size_t blen = dot ? (size_t)(dot - bn) : strlen(bn);
		out->name = strndup(bn, blen);
	}
	return rc;
}
