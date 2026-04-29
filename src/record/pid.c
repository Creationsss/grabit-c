// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/pid.h"

#include "log.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char PID_FILE[] = "/tmp/grabit_recording.pid";

static pid_t read_pid_file(void) {
	FILE *f = fopen(PID_FILE, "r");
	if (!f) return 0;
	char buf[32] = {0};
	if (!fgets(buf, sizeof buf, f)) {
		fclose(f);
		return 0;
	}
	fclose(f);
	char *end = NULL;
	long n = strtol(buf, &end, 10);
	if (end == buf || n <= 0 || n > INT_MAX) return 0;
	return (pid_t)n;
}

int write_pid_file_excl(pid_t p) {
	int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_EXCL, 0644);
	if (fd < 0) return -1;
	char buf[32];
	int n = snprintf(buf, sizeof buf, "%d\n", (int)p);
	if (n < 0) { close(fd); unlink(PID_FILE); return -1; }
	ssize_t w = write(fd, buf, (size_t)n);
	close(fd);
	if (w != n) { unlink(PID_FILE); return -1; }
	return 0;
}

void unlink_pid_file(void) {
	unlink(PID_FILE);
}

static bool process_alive(pid_t p) {
	if (p <= 0) return false;
	if (kill(p, 0) == 0) return true;
	return errno != ESRCH;
}

static bool is_grabit_process(pid_t p) {
	if (p <= 0) return false;
	char path[64];
	snprintf(path, sizeof path, "/proc/%d/comm", (int)p);
	FILE *f = fopen(path, "r");
	if (!f) return false;
	char comm[32] = {0};
	bool ok = fgets(comm, sizeof comm, f) != NULL;
	fclose(f);
	if (!ok) return false;
	char *nl = strchr(comm, '\n');
	if (nl) *nl = '\0';
	return strcmp(comm, "grabit") == 0;
}

int stop_running_recording(void) {
	pid_t prev = read_pid_file();
	if (prev <= 0) return -1;
	if (!process_alive(prev) || !is_grabit_process(prev)) {
		log_warn("removing stale recording pidfile (pid %d not a running grabit)", (int)prev);
		unlink(PID_FILE);
		return -1;
	}
	log_info("stopping recording (pid %d)", (int)prev);
	if (kill(prev, SIGINT) != 0) {
		if (errno == EPERM) {
			log_error("recording PID %d not owned by us; cannot signal", (int)prev);
		} else {
			log_error("kill(%d): %s", (int)prev, strerror(errno));
		}
		return -1;
	}
	return 0;
}
