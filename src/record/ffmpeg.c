// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/ffmpeg.h"

#include "log.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int waitpid_intr(pid_t pid, int *status, const char *what) {
	while (waitpid(pid, status, 0) < 0) {
		if (errno == EINTR) continue;
		log_error("waitpid(%s): %s", what, strerror(errno));
		return -1;
	}
	return 0;
}

int spawn_ffmpeg(const char *ffmpeg_bin,
                 int width, int height, int fps, int crf,
                 const char *output_path,
                 pid_t *child_pid, int *write_fd) {
	int p[2];
	if (pipe(p) != 0) {
		log_error("pipe: %s", strerror(errno));
		return -1;
	}

	pid_t pid = fork();
	if (pid < 0) {
		log_error("fork: %s", strerror(errno));
		close(p[0]);
		close(p[1]);
		return -1;
	}

	if (pid == 0) {
		setpgid(0, 0);
		if (dup2(p[0], STDIN_FILENO) < 0) _exit(126);
		close(p[0]);
		close(p[1]);

		char size[32], rate[16], crf_s[8];
		snprintf(size, sizeof size, "%dx%d", width, height);
		snprintf(rate, sizeof rate, "%d", fps);
		snprintf(crf_s, sizeof crf_s, "%d", crf);

		char *argv[] = {
			(char *)ffmpeg_bin,
			(char *)"-loglevel", (char *)"error",
			(char *)"-y",
			(char *)"-f", (char *)"rawvideo",
			(char *)"-pix_fmt", (char *)"bgra",
			(char *)"-s", size,
			(char *)"-framerate", rate,
			(char *)"-i", (char *)"-",
			(char *)"-vf", (char *)"crop=trunc(iw/2)*2:trunc(ih/2)*2",
			(char *)"-c:v", (char *)"libx264",
			(char *)"-preset", (char *)"ultrafast",
			(char *)"-pix_fmt", (char *)"yuv420p",
			(char *)"-crf", crf_s,
			(char *)output_path,
			NULL,
		};
		execvp(ffmpeg_bin, argv);
		_exit(127);
	}

	(void)setpgid(pid, pid);

	close(p[0]);
	*child_pid = pid;
	*write_fd  = p[1];
	return 0;
}

int wait_ffmpeg(pid_t pid) {
	int status = 0;
	if (waitpid_intr(pid, &status, "ffmpeg") != 0) return -1;
	if (WIFEXITED(status)) {
		int code = WEXITSTATUS(status);
		if (code == 0) return 0;
		if (code == 127) log_error("ffmpeg not found in $PATH (install ffmpeg)");
		else             log_error("ffmpeg exited with code %d", code);
		return -1;
	}
	if (WIFSIGNALED(status)) {
		log_error("ffmpeg killed by signal %d", WTERMSIG(status));
		return -1;
	}
	return -1;
}

int compress_to_target_size(const char *ffmpeg_bin, const char *path,
                            int max_mb, double duration_secs) {
	if (duration_secs <= 0.0 || max_mb <= 0) return -1;

	long long target_bytes = (long long)max_mb * 1024 * 1024;
	long long target_bps   = (long long)((double)target_bytes * 8.0 / duration_secs * 0.95);
	if (target_bps < 100000) target_bps = 100000;

	struct stat orig;
	mode_t orig_mode = 0644;
	if (stat(path, &orig) == 0) orig_mode = orig.st_mode & 07777;

	char *tmp_path = NULL;
	if (grabit_xasprintf(&tmp_path, "%s.compressing.mp4", path) != 0) return -1;

	pid_t pid = fork();
	if (pid < 0) {
		log_error("fork: %s", strerror(errno));
		free(tmp_path);
		return -1;
	}
	if (pid == 0) {
		setpgid(0, 0);
		char bps_s[32];
		snprintf(bps_s, sizeof bps_s, "%lld", target_bps);

		char *argv[] = {
			(char *)ffmpeg_bin,
			(char *)"-loglevel", (char *)"error",
			(char *)"-y",
			(char *)"-i", (char *)path,
			(char *)"-c:v", (char *)"libx264",
			(char *)"-b:v", bps_s,
			(char *)"-maxrate", bps_s,
			(char *)"-bufsize", bps_s,
			(char *)"-preset", (char *)"medium",
			(char *)"-pix_fmt", (char *)"yuv420p",
			(char *)"-an",
			tmp_path,
			NULL,
		};
		execvp(ffmpeg_bin, argv);
		_exit(127);
	}
	(void)setpgid(pid, pid);

	int status = 0;
	if (waitpid_intr(pid, &status, "ffmpeg compress") != 0) {
		unlink(tmp_path);
		free(tmp_path);
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		log_error("ffmpeg compress failed");
		unlink(tmp_path);
		free(tmp_path);
		return -1;
	}

	if (rename(tmp_path, path) != 0) {
		log_error("rename(%s -> %s): %s", tmp_path, path, strerror(errno));
		unlink(tmp_path);
		free(tmp_path);
		return -1;
	}
	(void)chmod(path, orig_mode);
	free(tmp_path);
	return 0;
}
