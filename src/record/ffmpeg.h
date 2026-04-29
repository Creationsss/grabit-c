// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_FFMPEG_H
#define GRABIT_RECORD_FFMPEG_H

#include <sys/types.h>

int spawn_ffmpeg(const char *ffmpeg_bin,
                 int width, int height, int fps, int crf,
                 const char *output_path,
                 pid_t *child_pid, int *write_fd);

int wait_ffmpeg(pid_t pid);

int compress_to_target_size(const char *ffmpeg_bin, const char *path,
                            int max_mb, double duration_secs);

#endif
