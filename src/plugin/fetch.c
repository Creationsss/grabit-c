// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "plugin/fetch.h"

#include "log.h"
#include "vendor/sha256/sha256.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <curl/curl.h>

static size_t curl_to_file(void *ptr, size_t sz, size_t nm, void *ud) {
	FILE *f = ud;
	return fwrite(ptr, sz, nm, f);
}

enum plugin_fetch_result plugin_fetch_url(const char *url, const char *dst,
										  time_t if_modified_since) {
	CURL *c = curl_easy_init();
	if (!c) return PLUGIN_FETCH_FAIL;
	FILE *f = fopen(dst, "wb");
	if (!f) {
		curl_easy_cleanup(c);
		return PLUGIN_FETCH_FAIL;
	}
	curl_easy_setopt(c, CURLOPT_URL, url);
	curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curl_to_file);
	curl_easy_setopt(c, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(c, CURLOPT_FILETIME, 1L);
	if (if_modified_since > 0) {
		curl_easy_setopt(c, CURLOPT_TIMECONDITION, (long)CURL_TIMECOND_IFMODSINCE);
		curl_easy_setopt(c, CURLOPT_TIMEVALUE, (long)if_modified_since);
	}

	CURLcode rc = curl_easy_perform(c);
	long unmet = 0;
	curl_easy_getinfo(c, CURLINFO_CONDITION_UNMET, &unmet);
	fclose(f);
	curl_easy_cleanup(c);

	if (unmet) {
		unlink(dst);
		return PLUGIN_FETCH_NOT_MODIFIED;
	}
	if (rc != CURLE_OK) {
		log_error("plugin: download %s: %s", url, curl_easy_strerror(rc));
		unlink(dst);
		return PLUGIN_FETCH_FAIL;
	}
	return PLUGIN_FETCH_OK;
}

int plugin_sha256_file(const char *path, char *hex_out) {
	FILE *f = fopen(path, "rb");
	if (!f) return -1;
	struct sha256_ctx ctx;
	sha256_init(&ctx);
	uint8_t buf[8192];
	size_t got;
	while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
		sha256_update(&ctx, buf, got);
	}
	int err = ferror(f);
	fclose(f);
	if (err) return -1;
	uint8_t digest[SHA256_DIGEST_SIZE];
	sha256_final(&ctx, digest);
	sha256_to_hex(digest, hex_out);
	return 0;
}

bool plugin_sha256_equal(const char *expect_hex, const char *actual_hex) {
	if (!expect_hex || !*expect_hex) return true;
	return strcasecmp(expect_hex, actual_hex) == 0;
}
