// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/ring.h"

#include "log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int pool_init(struct buf_pool *p, size_t n, size_t buf_size) {
	if (n == 0 || n > POOL_CAP) return -1;
	memset(p, 0, sizeof *p);
	p->n = n;
	p->buf_size = buf_size;
	pthread_mutex_init(&p->mu, NULL);
	pthread_cond_init(&p->cv, NULL);
	for (size_t i = 0; i < n; i++) {
		p->slots[i] = malloc(buf_size);
		if (!p->slots[i]) {
			pool_destroy(p);
			return -1;
		}
	}
	return 0;
}

void pool_destroy(struct buf_pool *p) {
	for (size_t i = 0; i < p->n; i++)
		free(p->slots[i]);
	pthread_cond_destroy(&p->cv);
	pthread_mutex_destroy(&p->mu);
	memset(p, 0, sizeof *p);
}

void *pool_acquire(struct buf_pool *p) {
	pthread_mutex_lock(&p->mu);
	for (;;) {
		for (size_t i = 0; i < p->n; i++) {
			if (!p->busy[i]) {
				p->busy[i] = true;
				void *ret = p->slots[i];
				pthread_mutex_unlock(&p->mu);
				return ret;
			}
		}
		pthread_cond_wait(&p->cv, &p->mu);
	}
}

void *pool_try_acquire(struct buf_pool *p) {
	pthread_mutex_lock(&p->mu);
	for (size_t i = 0; i < p->n; i++) {
		if (!p->busy[i]) {
			p->busy[i] = true;
			void *ret = p->slots[i];
			pthread_mutex_unlock(&p->mu);
			return ret;
		}
	}
	pthread_mutex_unlock(&p->mu);
	return NULL;
}

void pool_release(struct buf_pool *p, void *buf) {
	pthread_mutex_lock(&p->mu);
	for (size_t i = 0; i < p->n; i++) {
		if (p->slots[i] == buf) {
			p->busy[i] = false;
			pthread_cond_signal(&p->cv);
			break;
		}
	}
	pthread_mutex_unlock(&p->mu);
}

void frame_release(struct frame *f) {
	if (!f || !f->data) return;
	if (f->pool)
		pool_release(f->pool, f->data);
	else
		free(f->data);
	f->data = NULL;
}

void ring_init(struct ring *r) {
	memset(r, 0, sizeof *r);
	pthread_mutex_init(&r->mu, NULL);
	pthread_cond_init(&r->cv_data, NULL);
}

void ring_destroy(struct ring *r) {
	for (size_t i = r->tail; i != r->head; i = (i + 1) % RING_CAP) {
		frame_release(&r->slots[i]);
	}
	pthread_cond_destroy(&r->cv_data);
	pthread_mutex_destroy(&r->mu);
}

void ring_push(struct ring *r, const struct frame *f) {
	pthread_mutex_lock(&r->mu);
	size_t next = (r->head + 1) % RING_CAP;
	if (next == r->tail) {
		frame_release(&r->slots[r->tail]);
		r->tail = (r->tail + 1) % RING_CAP;
		r->dropped++;
	}
	r->slots[r->head] = *f;
	r->head = next;
	r->pushed++;
	pthread_cond_signal(&r->cv_data);
	pthread_mutex_unlock(&r->mu);
}

int ring_pop(struct ring *r, struct frame *out) {
	pthread_mutex_lock(&r->mu);
	while (r->head == r->tail && !r->stopped) {
		pthread_cond_wait(&r->cv_data, &r->mu);
	}
	if (r->head == r->tail) {
		pthread_mutex_unlock(&r->mu);
		return -1;
	}
	*out = r->slots[r->tail];
	r->tail = (r->tail + 1) % RING_CAP;
	r->popped++;
	pthread_mutex_unlock(&r->mu);
	return 0;
}

void ring_stop(struct ring *r) {
	pthread_mutex_lock(&r->mu);
	r->stopped = true;
	pthread_cond_broadcast(&r->cv_data);
	pthread_mutex_unlock(&r->mu);
}

void ring_record_drop(struct ring *r) {
	pthread_mutex_lock(&r->mu);
	r->dropped++;
	pthread_mutex_unlock(&r->mu);
}

static int write_all(int fd, const uint8_t *p, size_t n) {
	while (n > 0) {
		ssize_t w = write(fd, p, n);
		if (w < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		p += w;
		n -= (size_t)w;
	}
	return 0;
}

void *encoder_thread(void *arg) {
	struct enc_state *e = arg;
	struct frame f;
	while (ring_pop(e->ring, &f) == 0) {
		if (!e->write_failed && e->write_fd >= 0 && f.data) {
			size_t row_bytes = (size_t)f.width * 4;
			if ((size_t)f.stride == row_bytes) {
				if (write_all(e->write_fd, f.data, row_bytes * (size_t)f.height) < 0) {
					log_error("recording: write to ffmpeg: %s", strerror(errno));
					e->write_failed = true;
					if (e->stop) *e->stop = 1;
				}
			} else {
				const uint8_t *base = f.data;
				for (int row = 0; row < f.height && !e->write_failed; row++) {
					if (write_all(e->write_fd, base + (size_t)row * (size_t)f.stride,
								  row_bytes) < 0) {
						log_error("recording: write to ffmpeg: %s", strerror(errno));
						e->write_failed = true;
						if (e->stop) *e->stop = 1;
						break;
					}
				}
			}
		}
		frame_release(&f);
	}
	return NULL;
}
