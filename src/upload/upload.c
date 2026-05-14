// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "upload/upload.h"

#include "args.h"
#include "config.h"
#include "log.h"
#include "notify/notify.h"
#include "upload/sxcu.h"
#include "util.h"
#include "util/json_path.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

struct service {
	const char *name;
	const char *url;
	const char *auth_name;
	const char *json_path;
	bool auth_in_form;
};

static const struct service SERVICES[] = {
	{"zipline", NULL, "authorization", "files[0].url", false},
	{"nest", "https://nest.rip/api/files/upload", "Authorization", "fileURL", false},
	{"fakecrime", "https://upload.fakecrime.bio", "Secret", "url|data.url", false},
	{"ez", "https://api.e-z.host/files", "key", "imageUrl", false},
	{"guns", "https://guns.lol/api/upload", "key", "link", true},
	{"pixelvault", "https://pixelvault.co/", "Authorization", "resource", false},
};
static const size_t N_SERVICES = sizeof SERVICES / sizeof SERVICES[0];

static void build_auth_keys(const char *service, char *env_key, size_t env_cap,
							char *cfg_key, size_t cfg_cap) {
	snprintf(env_key, env_cap, "GRABIT_%s_AUTH", service);
	for (char *p = env_key + 7; *p; p++)
		*p = (char)toupper((unsigned char)*p);
	snprintf(cfg_key, cfg_cap, "services.%s.auth", service);
}

static const struct service *find_service(const char *name) {
	for (size_t i = 0; i < N_SERVICES; i++) {
		if (strcmp(SERVICES[i].name, name) == 0) return &SERVICES[i];
	}
	return NULL;
}

bool upload_service_known(const char *name) {
	if (!name) return false;
	return find_service(name) != NULL || sxcu_dir_has(name);
}

int upload_preflight(struct config *cfg, const struct args *a, const char **service_out) {
	const char *service = a->service;
	if (!service) service = config_get(cfg, "service");
	if (!service || !service[0]) {
		log_error("no service: pass --<service> or `grabit set service <name>`");
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "run: grabit set service zipline (or nest, fakecrime, ez, guns, pixelvault)",
			.force = true,
		});
		return -1;
	}
	if (!upload_service_known(service)) {
		log_error("unknown service: %s", service);
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = "valid services: zipline, nest, fakecrime, ez, guns, pixelvault",
			.force = true,
		});
		return -1;
	}

	char env_key[64], cfg_key[64];
	build_auth_keys(service, env_key, sizeof env_key, cfg_key, sizeof cfg_key);
	const char *env_auth = getenv(env_key);
	const char *cfg_auth = config_get(cfg, cfg_key);
	if ((!env_auth || !env_auth[0]) && (!cfg_auth || !cfg_auth[0])) {
		log_error("no auth token for %s.", service);
		log_error("  recommended (password-manager-friendly):");
		log_error("    export %s=\"$(pass show grabit/%s)\"", env_key, service);
		log_error("  or fallback (plaintext in config 0600):");
		log_error("    grabit set %s <token>", cfg_key);
		char body[160];
		snprintf(body, sizeof body, "run: grabit set services.%s.auth <token>", service);
		notify_send(&(struct notify_opts){
			.summary = "grabit: setup needed",
			.body = body,
			.force = true,
		});
		return -1;
	}

	if (strcmp(service, "zipline") == 0) {
		const char *domain = config_get(cfg, "services.zipline.domain");
		if (!domain || !domain[0]) {
			log_error("zipline requires services.zipline.domain (e.g. https://example.com/api/upload)");
			log_error("    grabit set services.zipline.domain https://<host>/api/upload");
			notify_send(&(struct notify_opts){
				.summary = "grabit: setup needed",
				.body = "run: grabit set services.zipline.domain https://<host>/api/upload",
				.force = true,
			});
			return -1;
		}
	}

	*service_out = service;
	return 0;
}

static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *user) {
	struct grabit_buf *b = user;
	size_t total = size * nmemb;
	return grabit_buf_putn(b, ptr, total) == 0 ? total : 0;
}

static char *extract_url(struct json_object *root, const char *paths) {
	const char *p = paths;
	while (*p) {
		const char *bar = strchr(p, '|');
		size_t len = bar ? (size_t)(bar - p) : strlen(p);
		char one[256];
		if (len >= sizeof one) return NULL;
		memcpy(one, p, len);
		one[len] = '\0';
		char *got = grabit_json_path_string(root, one);
		if (got) return got;
		if (!bar) break;
		p = bar + 1;
	}
	return NULL;
}

static struct curl_slist *append_header(struct curl_slist *list,
										const char *name, const char *value,
										bool *oom) {
	size_t n = strlen(name) + strlen(value) + 3;
	char *line = malloc(n);
	if (!line) {
		*oom = true;
		return list;
	}
	snprintf(line, n, "%s: %s", name, value);
	struct curl_slist *next = curl_slist_append(list, line);
	free(line);
	if (!next) *oom = true;
	return next ? next : list;
}

static void log_response_body(const char *body) {
	if (!body || !body[0]) return;
	enum { BODY_MAX = 512 };
	size_t len = strlen(body);
	if (len <= BODY_MAX) {
		log_error("response: %s", body);
		return;
	}
	log_error("response (truncated, %zu bytes): %.*s…", len, (int)BODY_MAX, body);
}

static void log_http_failure(long code, const char *body) {
	switch (code) {
	case 401:
		log_error("upload failed (401): authentication failed; check your auth token");
		break;
	case 413:
		log_error("upload failed (413): file too large; try compressing");
		break;
	case 422:
		log_error("upload failed (422): file rejected; invalid format or validation failure");
		break;
	case 429:
		log_error("upload failed (429): rate limited; wait a bit before retrying");
		break;
	case 500:
		log_error("upload failed (500): server error; try again later");
		break;
	default:
		if (code == 0)
			log_error("upload failed: no response from server");
		else
			log_error("upload failed (HTTP %ld)", code);
		log_response_body(body);
		break;
	}
}

void upload_result_free(struct upload_result *r) {
	if (!r) return;
	free(r->url);
	free(r->body);
	r->url = r->body = NULL;
	r->http_code = 0;
}

int upload_perform(const char *service_name, const char *file_path,
				   struct config *cfg, struct upload_result *out) {
	upload_result_free(out);

	const struct service *svc = find_service(service_name);
	if (!svc) {
		struct sxcu_uploader u = {0};
		if (sxcu_dir_lookup(service_name, &u) == 0) {
			int rc = sxcu_upload(&u, file_path, out);
			sxcu_free(&u);
			return rc;
		}
		log_error("unknown service: %s", service_name);
		return -1;
	}

	const char *url = svc->url;
	if (!url) {
		url = config_get(cfg, "services.zipline.domain");
		if (!url) {
			log_error("zipline requires services.zipline.domain (e.g. https://example.com/api/upload)");
			return -1;
		}
	}

	char env_key[64], cfg_key[64];
	build_auth_keys(svc->name, env_key, sizeof env_key, cfg_key, sizeof cfg_key);
	const char *auth = getenv(env_key);
	if (!auth || !auth[0]) auth = config_get(cfg, cfg_key);

	if (!auth || !auth[0]) {
		log_error("no auth token for %s.", svc->name);
		log_error("  recommended (password-manager-friendly):");
		log_error("    export %s=\"$(pass show grabit/%s)\"", env_key, svc->name);
		log_error("  or fallback (plaintext in config 0600):");
		log_error("    grabit set %s <token>", cfg_key);
		return -1;
	}

	CURL *curl = curl_easy_init();
	if (!curl) {
		log_error("curl_easy_init failed");
		return -1;
	}

	curl_mime *mime = curl_mime_init(curl);
	curl_mimepart *part = curl_mime_addpart(mime);
	curl_mime_name(part, "file");
	curl_mime_filedata(part, file_path);

	if (strcmp(svc->name, "nest") == 0) {
		const char *folder = config_get(cfg, "services.nest.folder");
		if (folder) {
			curl_mimepart *fp = curl_mime_addpart(mime);
			curl_mime_name(fp, "folder");
			curl_mime_data(fp, folder, CURL_ZERO_TERMINATED);
		}
	}

	struct curl_slist *headers = NULL;
	bool hdr_oom = false;
	if (svc->auth_in_form) {
		curl_mimepart *ap = curl_mime_addpart(mime);
		curl_mime_name(ap, svc->auth_name);
		curl_mime_data(ap, auth, CURL_ZERO_TERMINATED);
	} else {
		headers = append_header(headers, svc->auth_name, auth, &hdr_oom);
	}

	if (strcmp(svc->name, "zipline") == 0 && cfg) {
		const char *prefix = "services.zipline.headers.";
		size_t pl = strlen(prefix);
		bool has_format = false;
		for (size_t i = 0; i < cfg->n; i++) {
			const char *k = cfg->kvs[i].key;
			if (strncmp(k, prefix, pl) != 0) continue;
			const char *header_name = k + pl;
			const char *val = cfg->kvs[i].val;
			if (strcmp(header_name, "x-zipline-format") == 0) has_format = true;
			if (val && val[0]) headers = append_header(headers, header_name, val, &hdr_oom);
		}
		if (!has_format) {
			headers = append_header(headers, "x-zipline-format", "name", &hdr_oom);
		}
	}

	if (hdr_oom) {
		log_error("upload: header allocation failed");
		curl_slist_free_all(headers);
		curl_mime_free(mime);
		curl_easy_cleanup(curl);
		return -1;
	}

	struct grabit_buf body = {0};
	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "grabit/0.1.0");
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

	log_debug("POST %s (%s)", url, svc->name);
	CURLcode rc = curl_easy_perform(curl);
	long http_code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
	out->http_code = http_code;
	out->body = body.data;
	body.data = NULL;

	curl_slist_free_all(headers);
	curl_mime_free(mime);
	curl_easy_cleanup(curl);

	if (rc != CURLE_OK) {
		log_error("curl: %s", curl_easy_strerror(rc));
		return -1;
	}
	if (http_code != 200) {
		log_http_failure(http_code, out->body);
		return -1;
	}

	struct json_object *root = out->body
								   ? json_tokener_parse(out->body)
								   : NULL;
	if (!root || json_object_get_type(root) != json_type_object) {
		log_error("invalid JSON response from %s", svc->name);
		log_response_body(out->body);
		if (root) json_object_put(root);
		return -1;
	}

	out->url = extract_url(root, svc->json_path);
	json_object_put(root);

	if (!out->url) {
		log_error("could not find %s in %s response", svc->json_path, svc->name);
		log_response_body(out->body);
		return -1;
	}
	return 0;
}
