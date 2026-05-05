// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#ifndef GRABIT_RECORD_RING_H
#define GRABIT_RECORD_RING_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RING_CAP 16
#define POOL_CAP (RING_CAP + 2)

struct buf_pool {
	void *slots[POOL_CAP];
	bool busy[POOL_CAP];
	size_t n;
	size_t buf_size;
	pthread_mutex_t mu;
};

int pool_init(struct buf_pool *p, size_t n, size_t buf_size);
void pool_destroy(struct buf_pool *p);
void *pool_try_acquire(struct buf_pool *p);
void pool_release(struct buf_pool *p, void *buf);

struct frame {
	void *data;
	int32_t width;
	int32_t height;
	int32_t stride;
	uint32_t format;
	struct buf_pool *pool;
};

void frame_release(struct frame *f);

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
	atomic_int *stop;
};

void ring_init(struct ring *r);
void ring_destroy(struct ring *r);
void ring_push(struct ring *r, const struct frame *f);
int ring_pop(struct ring *r, struct frame *out);
void ring_stop(struct ring *r);
void ring_record_drop(struct ring *r);

void *encoder_thread(void *arg);

#endif
