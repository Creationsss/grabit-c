// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "ocr/ocr.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int grabit_ocr_check(const char *bin) {
	if (!bin || !bin[0]) return -1;

	pid_t pid = fork();
	if (pid < 0) return -1;
	if (pid == 0) {
		grabit_redirect_stdio_devnull();
		char *argv[] = {(char *)bin, (char *)"--version", NULL};
		execvp(bin, argv);
		_exit(errno == ENOENT ? 127 : 126);
	}
	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) return -1;
	if (!WIFEXITED(status)) {
		log_debug("ocr: tesseract --version killed by signal %d", WTERMSIG(status));
		return -1;
	}
	int code = WEXITSTATUS(status);
	log_debug("ocr: tesseract --version exit code = %d", code);
	if (code == 127) return -1;
	if (code == 126) return -1;
	return 0;
}

char *grabit_ocr_run(const char *bin, const char *path) {
	if (!bin || !bin[0] || !path || !path[0]) return NULL;

	int p[2];
	if (pipe(p) != 0) {
		log_error("ocr: pipe: %s", strerror(errno));
		return NULL;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("ocr: fork: %s", strerror(errno));
		close(p[0]);
		close(p[1]);
		return NULL;
	}
	if (pid == 0) {
		if (dup2(p[1], STDOUT_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);
		char *argv[] = {(char *)bin, (char *)path, (char *)"stdout",
						(char *)"-l", (char *)"eng", NULL};
		execvp(bin, argv);
		_exit(127);
	}
	close(p[1]);

	struct grabit_buf buf = {0};
	char chunk[4096];
	for (;;) {
		ssize_t n = read(p[0], chunk, sizeof chunk);
		if (n < 0) {
			if (errno == EINTR) continue;
			break;
		}
		if (n == 0) break;
		if (grabit_buf_putn(&buf, chunk, (size_t)n) != 0) {
			grabit_buf_free(&buf);
			close(p[0]);
			kill(pid, SIGTERM);
			(void)grabit_waitpid_intr(pid, NULL);
			log_error("ocr: oom reading tesseract output");
			return NULL;
		}
	}
	close(p[0]);

	int status = 0;
	if (grabit_waitpid_intr(pid, &status) != 0) {
		grabit_buf_free(&buf);
		return NULL;
	}
	if (!WIFEXITED(status)) {
		grabit_buf_free(&buf);
		log_error("ocr: tesseract killed by signal %d", WTERMSIG(status));
		return NULL;
	}
	int code = WEXITSTATUS(status);
	if (code != 0) {
		grabit_buf_free(&buf);
		if (code == 127) {
			log_error("ocr: tesseract not found in $PATH");
		} else {
			log_error("ocr: tesseract exited with code %d (see stderr above)", code);
		}
		return NULL;
	}

	if (!buf.data) {
		char *empty = malloc(1);
		if (empty) empty[0] = '\0';
		return empty;
	}

	size_t n = buf.len;
	while (n > 0 && (buf.data[n - 1] == '\n' || buf.data[n - 1] == '\r' ||
					 buf.data[n - 1] == ' ' || buf.data[n - 1] == '\t')) {
		n--;
	}
	buf.data[n] = '\0';
	return buf.data;
}
