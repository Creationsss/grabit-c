// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "record/record.h"

#include "args.h"
#include "capture/capture.h"
#include "clipboard/clipboard.h"
#include "config.h"
#include "log.h"
#include "notify/notify.h"
#include "paths.h"
#include "record/compose.h"
#include "record/ffmpeg.h"
#include "record/overlay.h"
#include "record/pid.h"
#include "record/ring.h"
#include "region/region.h"
#include "tray/tray.h"
#include "upload/upload.h"
#include "wl.h"

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static atomic_int g_stop;

static void on_record_signal(int sig) {
	(void)sig;
	atomic_store(&g_stop, 1);
}

struct prev_sigs {
	struct sigaction sigint;
	struct sigaction sigterm;
	struct sigaction sighup;
	struct sigaction sigpipe;
};

static void install_signal_handlers(struct prev_sigs *prev) {
	struct sigaction sa = {0};
	sa.sa_handler = on_record_signal;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGINT, &sa, &prev->sigint);
	sigaction(SIGTERM, &sa, &prev->sigterm);
	sigaction(SIGHUP, &sa, &prev->sighup);

	struct sigaction ign = {0};
	ign.sa_handler = SIG_IGN;
	sigemptyset(&ign.sa_mask);
	sigaction(SIGPIPE, &ign, &prev->sigpipe);
}

static void restore_signal_handlers(const struct prev_sigs *prev) {
	sigaction(SIGINT, &prev->sigint, NULL);
	sigaction(SIGTERM, &prev->sigterm, NULL);
	sigaction(SIGHUP, &prev->sighup, NULL);
	sigaction(SIGPIPE, &prev->sigpipe, NULL);
}

static int read_int_cfg(struct config *cfg, const char *key, int def, int lo, int hi) {
	const char *v = config_get(cfg, key);
	if (!v || !v[0]) return def;
	long n = strtol(v, NULL, 10);
	if (n < lo) return def;
	if (n > hi) return hi;
	return (int)n;
}

static int read_fps(struct config *cfg) {
	return read_int_cfg(cfg, "recording.fps", 30, 1, 120);
}

static int read_crf(struct config *cfg) {
	return read_int_cfg(cfg, "recording.crf", 20, 0, 51);
}

static bool read_cursor(struct config *cfg) {
	const char *v = config_get(cfg, "recording.cursor");
	return !v || strcmp(v, "true") == 0;
}

static const char *read_ffmpeg(struct config *cfg) {
	const char *v = config_get(cfg, "recording.ffmpeg");
	return (v && v[0]) ? v : "ffmpeg";
}

static const char *read_preset(struct config *cfg) {
	const char *v = config_get(cfg, "recording.preset");
	return (v && v[0]) ? v : "fast";
}

static const char *read_tune(struct config *cfg) {
	const char *v = config_get(cfg, "recording.tune");
	return (v && v[0]) ? v : NULL;
}

static const char *read_pix_fmt(struct config *cfg) {
	const char *v = config_get(cfg, "recording.pix_fmt");
	return (v && v[0]) ? v : "yuv420p";
}

static int64_t now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static char *build_record_path(struct config *cfg, const struct args *a) {
	return paths_build_output(cfg, a->filename_tpl, ".mp4", PATHS_DEST_VIDEOS);
}

static int capture_loop(struct grabit_wl_state *s, const struct rec_layout *layout,
						struct buf_pool *pool, int fps, bool cursor, struct ring *ring) {
	int64_t period_ns = 1000000000 / fps;
	int64_t start_ns = now_ns();
	int64_t frame_idx = 0;
	int consec_fail = 0;
	bool direct = rec_layout_is_direct(layout);

	while (!g_stop) {
		int64_t deadline = start_ns + frame_idx * period_ns;
		int64_t cur = now_ns();
		if (deadline > cur) {
			struct timespec ts = {
				.tv_sec = deadline / 1000000000,
				.tv_nsec = deadline % 1000000000,
			};
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
			if (g_stop) break;
		} else if (cur - deadline > period_ns * 4) {
			frame_idx = (cur - start_ns) / period_ns;
		}

		void *frame_buf = NULL;
		int32_t frame_stride = layout->dst_stride;
		struct buf_pool *frame_pool = NULL;
		int rc;
		if (direct) {
			rc = rec_layout_capture_direct(s, layout, cursor, &frame_buf, &frame_stride);
		} else {
			frame_buf = pool_try_acquire(pool);
			if (!frame_buf) {
				ring_record_drop(ring);
				frame_idx++;
				continue;
			}
			rc = rec_layout_capture_compose(s, layout, cursor, frame_buf);
			if (rc != 0) {
				pool_release(pool, frame_buf);
				frame_buf = NULL;
			} else {
				frame_pool = pool;
			}
		}

		if (rc != 0) {
			if (++consec_fail == 1) log_warn("recording: frame capture failed");
			if (consec_fail > 30) {
				log_error("recording: too many consecutive capture failures; stopping");
				g_stop = 1;
				break;
			}
			frame_idx++;
			continue;
		}
		consec_fail = 0;

		struct frame f = {
			.data = frame_buf,
			.width = layout->dst_w,
			.height = layout->dst_h,
			.stride = frame_stride,
			.format = WL_SHM_FORMAT_ARGB8888,
			.pool = frame_pool,
		};
		ring_push(ring, &f);
		frame_idx++;
	}

	return 0;
}

int record_toggle(struct config *cfg, const struct args *a) {
	if (stop_running_recording() == 0) return 0;

	const char *upload_service = NULL;
	if (!a->no_upload) {
		const char *def_action = config_get(cfg, "default_action");
		bool default_is_upload = def_action && strcmp(def_action, "upload") == 0;
		if (a->service || default_is_upload) {
			if (upload_preflight(cfg, a, &upload_service) != 0) return 1;
		}
	}

	struct grabit_wl_state s;
	if (grabit_wl_init(&s) != 0) {
		notify_send(&(struct notify_opts){
			.summary = "grabit",
			.body = "could not connect to wayland compositor",
			.force = true,
		});
		return 1;
	}

	struct image *frozen = calloc(s.n_outputs, sizeof *frozen);
	if (!frozen) {
		grabit_wl_finish(&s);
		log_error("oom");
		return 1;
	}
	for (size_t i = 0; i < s.n_outputs; i++) {
		if (capture_output_full(&s, s.outputs[i], &frozen[i]) != 0) {
			log_warn("freeze capture of %s failed; selector will be dimmed",
					 s.outputs[i]->name ? s.outputs[i]->name : "?");
			memset(&frozen[i], 0, sizeof frozen[i]);
			continue;
		}
		if (image_apply_transform(&frozen[i], s.outputs[i]->transform) != 0) {
			log_warn("freeze transform of %s failed; output may look skewed",
					 s.outputs[i]->name ? s.outputs[i]->name : "?");
		}
	}

	struct rect r;
	int rc = region_select(&s, frozen, &r);
	for (size_t i = 0; i < s.n_outputs; i++)
		image_free(&frozen[i]);
	free(frozen);

	if (rc != 0 || r.w <= 0 || r.h <= 0) {
		grabit_wl_finish(&s);
		log_info("recording cancelled");
		notify_send(&(struct notify_opts){
			.summary = "Recording cancelled",
			.force = true,
		});
		return 0;
	}

	struct rec_layout layout = {0};
	if (rec_layout_build(&s, r, &layout) != 0) {
		log_error("region does not overlap any output");
		grabit_wl_finish(&s);
		return 1;
	}

	char *output_path = build_record_path(cfg, a);
	if (!output_path) {
		log_error("recording: could not build output path");
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	int fps = read_fps(cfg);
	int crf = read_crf(cfg);
	bool cursor = read_cursor(cfg);
	const char *ffmpeg_bin = read_ffmpeg(cfg);
	const char *preset = read_preset(cfg);
	const char *tune = read_tune(cfg);
	const char *pix_fmt = read_pix_fmt(cfg);

	if (write_pid_file_excl(getpid()) != 0) {
		if (errno == EEXIST) {
			log_error("another grabit recording started concurrently; aborting this one");
		} else {
			log_error("could not write recording pidfile: %s", strerror(errno));
		}
		free(output_path);
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	pid_t ffmpeg_pid = -1;
	int ffmpeg_fd = -1;
	if (spawn_ffmpeg(ffmpeg_bin, preset, tune, pix_fmt,
					 layout.dst_w, layout.dst_h, fps, crf,
					 output_path, &ffmpeg_pid, &ffmpeg_fd) != 0) {
		unlink_pid_file();
		free(output_path);
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	g_stop = 0;
	struct prev_sigs prev = {0};
	install_signal_handlers(&prev);

	struct ring ring;
	ring_init(&ring);
	struct enc_state es = {
		.ring = &ring,
		.write_fd = ffmpeg_fd,
		.stop = &g_stop,
	};

	pthread_t enc;
	if (pthread_create(&enc, NULL, encoder_thread, &es) != 0) {
		log_error("pthread_create: %s", strerror(errno));
		restore_signal_handlers(&prev);
		ring_destroy(&ring);
		close(ffmpeg_fd);
		(void)wait_ffmpeg(ffmpeg_pid);
		unlink_pid_file();
		unlink(output_path);
		free(output_path);
		rec_layout_free(&layout);
		grabit_wl_finish(&s);
		return 1;
	}

	log_info("recording %dx%d (%zu output%s) @ %d fps → %s; re-run `grabit --record` to stop",
			 layout.dst_w, layout.dst_h, layout.n, layout.n == 1 ? "" : "s",
			 fps, output_path);

	struct buf_pool pool = {0};
	bool pool_used = !rec_layout_is_direct(&layout);
	if (pool_used) {
		size_t buf_size = (size_t)layout.dst_stride * (size_t)layout.dst_h;
		if (pool_init(&pool, POOL_CAP, buf_size) != 0) {
			log_error("recording: could not allocate frame pool");
			ring_stop(&ring);
			pthread_join(enc, NULL);
			restore_signal_handlers(&prev);
			ring_destroy(&ring);
			close(ffmpeg_fd);
			(void)wait_ffmpeg(ffmpeg_pid);
			unlink_pid_file();
			unlink(output_path);
			free(output_path);
			rec_layout_free(&layout);
			grabit_wl_finish(&s);
			return 1;
		}
	}

	struct overlay_state *overlay = overlay_start(&s, r);
	struct tray_state *tray = a->no_tray ? NULL : tray_start();

	int64_t t0 = now_ns();
	capture_loop(&s, &layout, pool_used ? &pool : NULL, fps, cursor, &ring);
	int64_t t1 = now_ns();

	tray_stop(tray);
	overlay_stop(overlay);

	ring_stop(&ring);
	pthread_join(enc, NULL);

	if (ffmpeg_fd >= 0) {
		close(ffmpeg_fd);
		ffmpeg_fd = -1;
	}
	int wait_rc = wait_ffmpeg(ffmpeg_pid);

	double secs = (t1 - t0) / 1e9;
	log_info("recording: %zu frames captured, %zu encoded, %zu dropped (%.2fs)",
			 ring.pushed, ring.popped, ring.dropped, secs);

	if (wait_rc == 0) {
		int max_mb = read_int_cfg(cfg, "recording.max_size_mb", 0, 0, 100000);
		struct stat st;
		if (max_mb > 0 && stat(output_path, &st) == 0 &&
			(long long)st.st_size > (long long)max_mb * 1024 * 1024) {
			log_info("recording: %lld bytes > %d MiB, compressing...",
					 (long long)st.st_size, max_mb);
			notify_send(&(struct notify_opts){
				.summary = "Recording compressing",
				.body = output_path,
				.force = true,
			});
			if (compress_to_target_size(ffmpeg_bin, output_path, max_mb, secs, &g_stop) == 0) {
				if (stat(output_path, &st) == 0) {
					log_info("recording: compressed to %lld bytes",
							 (long long)st.st_size);
				}
			} else {
				log_warn("recording: compression failed; original kept");
			}
		}
		log_info("saved: %s", output_path);
		if (!upload_service) {
			notify_send(&(struct notify_opts){
				.summary = "Recording saved",
				.body = output_path,
				.force = true,
			});
		}

		if (upload_service) {
			notify_send(&(struct notify_opts){
				.summary = "Uploading recording",
				.body = upload_service,
				.force = true,
			});
			struct upload_result ur = {0};
			int up_rc = upload_perform(upload_service, output_path, cfg, &ur);
			if (up_rc == 0 && ur.url) {
				clipboard_set_text(ur.url);
				log_info("%s", ur.url);
				notify_send(&(struct notify_opts){
					.summary = "Recording uploaded",
					.body = ur.url,
					.force = true,
				});
			} else {
				log_error("recording upload failed; file kept at %s", output_path);
				notify_send(&(struct notify_opts){
					.summary = "Upload failed",
					.body = "recording kept on disk; see terminal",
					.force = true,
				});
			}
			upload_result_free(&ur);
		}
	} else {
		log_error("recording failed; output may be incomplete: %s", output_path);
		notify_send(&(struct notify_opts){
			.summary = "Recording failed",
			.body = "see terminal for details",
			.force = true,
		});
	}

	restore_signal_handlers(&prev);
	ring_destroy(&ring);
	if (pool_used) pool_destroy(&pool);
	unlink_pid_file();
	free(output_path);
	rec_layout_free(&layout);
	grabit_wl_finish(&s);
	return wait_rc == 0 ? 0 : 1;
}
