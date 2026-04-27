// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char HEADER[] =
	"// SPDX-License-Identifier: AGPL-3.0-or-later\n"
	"// Copyright (C) 2026 creations\n"
	"\n";

static const char FIRST_LINE[] = "// SPDX-License-Identifier: AGPL-3.0-or-later";

static bool g_apply;
static int  g_missing;
static int  g_applied;

static bool is_source(const char *path) {
	size_t n = strlen(path);
	if (n < 3 || path[n - 2] != '.') return false;
	return path[n - 1] == 'c' || path[n - 1] == 'h';
}

static bool has_header(const char *path) {
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char buf[256];
	char *line = fgets(buf, sizeof buf, f);
	fclose(f);
	if (!line) return false;
	return strncmp(line, FIRST_LINE, sizeof FIRST_LINE - 1) == 0;
}

static int prepend_header(const char *path) {
	FILE *in = fopen(path, "rb");
	if (!in) {
		perror(path);
		return -1;
	}

	if (fseek(in, 0, SEEK_END) != 0) {
		fclose(in);
		return -1;
	}
	long size = ftell(in);
	if (size < 0) {
		fclose(in);
		return -1;
	}
	rewind(in);

	char *body = malloc((size_t)size);
	if (!body) {
		fclose(in);
		return -1;
	}
	size_t got = fread(body, 1, (size_t)size, in);
	fclose(in);
	if (got != (size_t)size) {
		free(body);
		return -1;
	}

	char tmp[4096];
	int n = snprintf(tmp, sizeof tmp, "%s.XXXXXX", path);
	if (n < 0 || (size_t)n >= sizeof tmp) {
		free(body);
		return -1;
	}
	int fd = mkstemp(tmp);
	if (fd < 0) {
		perror("mkstemp");
		free(body);
		return -1;
	}

	FILE *out = fdopen(fd, "wb");
	if (!out) {
		close(fd);
		unlink(tmp);
		free(body);
		return -1;
	}
	if (fputs(HEADER, out) == EOF || fwrite(body, 1, (size_t)size, out) != (size_t)size) {
		fclose(out);
		unlink(tmp);
		free(body);
		return -1;
	}
	if (fflush(out) != 0 || fsync(fileno(out)) != 0) {
		fclose(out);
		unlink(tmp);
		free(body);
		return -1;
	}
	fclose(out);
	free(body);

	if (rename(tmp, path) != 0) {
		perror("rename");
		unlink(tmp);
		return -1;
	}
	return 0;
}

static int visit(const char *path, const struct stat *sb, int type, struct FTW *ftw) {
	(void)sb;
	(void)ftw;
	if (type != FTW_F) return 0;
	if (!is_source(path)) return 0;
	if (strstr(path, "/vendor/")) return 0;
	if (has_header(path)) return 0;

	if (g_apply) {
		if (prepend_header(path) == 0) {
			printf("added header: %s\n", path);
			g_applied++;
		}
	} else {
		fprintf(stderr, "missing header: %s\n", path);
		g_missing++;
	}
	return 0;
}

int main(int argc, char **argv) {
	const char *root = NULL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--apply") == 0)
			g_apply = true;
		else if (strcmp(argv[i], "--check") == 0)
			g_apply = false;
		else if (argv[i][0] != '-' && !root)
			root = argv[i];
		else {
			fprintf(stderr, "usage: %s [--check|--apply] <root>\n", argv[0]);
			return 2;
		}
	}
	if (!root) {
		fprintf(stderr, "usage: %s [--check|--apply] <root>\n", argv[0]);
		return 2;
	}

	if (nftw(root, visit, 16, FTW_PHYS) != 0) {
		fprintf(stderr, "nftw(%s): %s\n", root, strerror(errno));
		return 2;
	}

	if (g_apply) {
		printf("done — %d file(s) updated.\n", g_applied);
		return 0;
	}
	if (g_missing > 0) {
		fprintf(stderr, "\nrun: make apply-headers\n");
		return 1;
	}
	return 0;
}
