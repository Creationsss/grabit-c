// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_PID_H
#define GRABIT_RECORD_PID_H

#include <sys/types.h>

int write_pid_file_excl(pid_t p);
void unlink_pid_file(void);
int stop_running_recording(void);

#endif
