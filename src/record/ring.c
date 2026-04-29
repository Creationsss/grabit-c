// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/ring.h"

#include "log.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void ring_init(struct ring *r) {
	memset(r, 0, sizeof *r);
	pthread_mutex_init(&r->mu, NULL);
	pthread_cond_init(&r->cv_data, NULL);
}

void ring_destroy(struct ring *r) {
	for (size_t i = r->tail; i != r->head; i = (i + 1) % RING_CAP) {
		free(r->slots[i].data);
	}
	pthread_cond_destroy(&r->cv_data);
	pthread_mutex_destroy(&r->mu);
}

void ring_push(struct ring *r, const struct frame *f) {
	pthread_mutex_lock(&r->mu);
	size_t next = (r->head + 1) % RING_CAP;
	if (next == r->tail) {
		free(r->slots[r->tail].data);
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

void *encoder_thread(void *arg) {
	struct enc_state *e = arg;
	struct frame f;
	while (ring_pop(e->ring, &f) == 0) {
		if (!e->write_failed && e->write_fd >= 0 && f.data) {
			size_t row_bytes = (size_t)f.width * 4;
			const uint8_t *base = f.data;
			for (int row = 0; row < f.height && !e->write_failed; row++) {
				const uint8_t *rp = base + (size_t)row * (size_t)f.stride;
				size_t left = row_bytes;
				while (left > 0) {
					ssize_t n = write(e->write_fd, rp, left);
					if (n < 0) {
						if (errno == EINTR) continue;
						log_error("recording: write to ffmpeg: %s", strerror(errno));
						e->write_failed = true;
						if (e->stop) *e->stop = 1;
						break;
					}
					rp += n;
					left -= (size_t)n;
				}
			}
		}
		free(f.data);
	}
	return NULL;
}
