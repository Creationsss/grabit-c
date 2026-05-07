// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#include "notify/notify.h"

#include "config.h"
#include "log.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <dbus/dbus.h>

#define BUS_DEST "org.freedesktop.Notifications"
#define BUS_PATH "/org/freedesktop/Notifications"
#define BUS_IFACE "org.freedesktop.Notifications"
#define BUS_METHOD "Notify"

#define APP_NAME "grabit"
#define EXPIRE_DEFAULT (-1)
#define DBUS_TIMEOUT_MS 2000

static bool g_show;
static bool g_silent;
static bool g_warned_bus;
static bool g_warned_daemon;

void notify_init(struct config *cfg, bool silent) {
	g_silent = silent;
	g_warned_bus = false;
	g_warned_daemon = false;
	if (silent) {
		g_show = false;
		return;
	}
	const char *v = cfg ? config_get(cfg, "notifications") : NULL;
	g_show = !v || strcmp(v, "true") == 0;
}

static bool pack_notify_args(DBusMessage *msg, const struct notify_opts *o) {
	DBusMessageIter args;
	dbus_message_iter_init_append(msg, &args);

	const char *app = APP_NAME;
	const char *icon = o->icon_path ? o->icon_path : "";
	const char *summary = o->summary;
	const char *body = o->body ? o->body : "";
	dbus_uint32_t replaces = 0;
	dbus_int32_t expire = EXPIRE_DEFAULT;

	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &app)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_UINT32, &replaces)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &icon)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &summary)) return false;
	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_STRING, &body)) return false;

	DBusMessageIter actions;
	if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "s", &actions)) return false;
	if (!dbus_message_iter_close_container(&args, &actions)) return false;

	DBusMessageIter hints;
	if (!dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &hints)) return false;
	if (!dbus_message_iter_close_container(&args, &hints)) return false;

	if (!dbus_message_iter_append_basic(&args, DBUS_TYPE_INT32, &expire)) return false;
	return true;
}

void notify_send(const struct notify_opts *o) {
	if (!o || !o->summary) return;
	if (g_silent) return;
	if (!g_show && !o->force) return;

	DBusError err;
	dbus_error_init(&err);

	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		if (!g_warned_bus) {
			log_warn("notifications unavailable: no user dbus session (%s)",
					 err.message ? err.message : "unknown");
			g_warned_bus = true;
		}
		dbus_error_free(&err);
		return;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);

	DBusMessage *msg = dbus_message_new_method_call(BUS_DEST, BUS_PATH, BUS_IFACE, BUS_METHOD);
	if (!msg || !pack_notify_args(msg, o)) {
		log_warn("notify: oom building message");
		goto cleanup;
	}

	DBusMessage *reply = dbus_connection_send_with_reply_and_block(
		bus, msg, DBUS_TIMEOUT_MS, &err);
	if (!reply) {
		if (!g_warned_daemon) {
			const char *name = err.name ? err.name : "";
			if (strstr(name, "ServiceUnknown") || strstr(name, "NameHasNoOwner")) {
				log_warn("notifications unavailable: no notification daemon running "
						 "(install dunst, mako, or similar)");
				log_warn("  consider enabling shutter sound: grabit set sound.enabled true");
			} else {
				log_warn("notify: %s: %s", name[0] ? name : "(no name)",
						 err.message ? err.message : "send failed");
			}
			g_warned_daemon = true;
		}
	} else {
		dbus_message_unref(reply);
	}

cleanup:
	if (msg) dbus_message_unref(msg);
	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	dbus_error_free(&err);
}
