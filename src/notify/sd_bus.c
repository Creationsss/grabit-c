// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "notify/notify.h"

#include "config.h"
#include "log.h"
#include "sdbus.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BUS_DEST    "org.freedesktop.Notifications"
#define BUS_PATH    "/org/freedesktop/Notifications"
#define BUS_IFACE   "org.freedesktop.Notifications"
#define BUS_METHOD  "Notify"

#define APP_NAME    "grabit"
#define EXPIRE_DEFAULT (-1)

static bool g_show;

void notify_init(struct config *cfg) {
	const char *v = cfg ? config_get(cfg, "SHOW_NOTIFICATIONS") : NULL;
	g_show = v && strcmp(v, "true") == 0;
}

void notify_send(const struct notify_opts *o) {
	if (!o || !o->summary) return;
	if (!g_show && !o->force) return;

	sd_bus *bus = NULL;
	int rc = sd_bus_open_user(&bus);
	if (rc < 0) {
		log_debug("notify: sd_bus_open_user: %s", strerror(-rc));
		return;
	}

	sd_bus_message *msg = NULL;
	rc = sd_bus_message_new_method_call(bus, &msg,
	                                    BUS_DEST, BUS_PATH, BUS_IFACE, BUS_METHOD);
	if (rc < 0) goto cleanup;

	rc = sd_bus_message_append(msg, "susss",
	                           APP_NAME,
	                           (uint32_t)0,
	                           o->icon_path ? o->icon_path : "",
	                           o->summary,
	                           o->body ? o->body : "");
	if (rc < 0) goto cleanup;

	rc = sd_bus_message_append_strv(msg, NULL);
	if (rc < 0) goto cleanup;

	rc = sd_bus_message_open_container(msg, 'a', "{sv}");
	if (rc < 0) goto cleanup;
	rc = sd_bus_message_close_container(msg);
	if (rc < 0) goto cleanup;

	rc = sd_bus_message_append(msg, "i", (int32_t)EXPIRE_DEFAULT);
	if (rc < 0) goto cleanup;

	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	// 2s timeout: notifications are best-effort; don't block if the daemon is stuck.
	rc = sd_bus_call(bus, msg, 2000000, &err, &reply);
	if (rc < 0) {
		log_debug("notify: %s: %s", err.name ? err.name : "(no name)",
		          err.message ? err.message : strerror(-rc));
	}
	sd_bus_error_free(&err);
	sd_bus_message_unref(reply);

cleanup:
	sd_bus_message_unref(msg);
	sd_bus_unref(bus);
}
