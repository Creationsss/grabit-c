// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_RING_H
#define GRABIT_RECORD_RING_H

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RING_CAP 16

struct frame {
	void *data;
	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
};

struct ring {
	struct frame slots[RING_CAP];
	size_t head;
	size_t tail;
	bool stopped;
	size_t pushed;
	size_t popped;
	size_t dropped;
	pthread_mutex_t mu;
	pthread_cond_t cv_data;
};

struct enc_state {
	struct ring *ring;
	int write_fd;
	bool write_failed;
	volatile sig_atomic_t *stop;
};

void ring_init(struct ring *r);
void ring_destroy(struct ring *r);
void ring_push(struct ring *r, const struct frame *f);
int ring_pop(struct ring *r, struct frame *out);
void ring_stop(struct ring *r);

void *encoder_thread(void *arg);

#endif
