// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/sni.h"

#include "log.h"
#include "notify/notify.h"
#include "sdbus.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define WATCHER_DEST "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"

#define ITEM_PATH "/StatusNotifierItem"
#define ITEM_IFACE "org.kde.StatusNotifierItem"

struct sni_props {
	const char *category;
	const char *id;
	const char *title;
	const char *status;
	const char *icon_name;
	const char *overlay_icon_name;
	const char *attention_icon_name;
	uint32_t window_id;
	int item_is_menu;
};

static void notify_tray_unavailable(const char *body) {
	notify_send(&(struct notify_opts){
		.summary = "grabit: setup needed",
		.body = body,
		.force = true,
	});
}

static int activate_cb(sd_bus_message *m, void *userdata, sd_bus_error *err) {
	(void)userdata;
	(void)err;
	pid_t parent = getppid();
	if (parent > 1) kill(parent, SIGINT);
	return sd_bus_reply_method_return(m, NULL);
}

static int noop_cb(sd_bus_message *m, void *userdata, sd_bus_error *err) {
	(void)userdata;
	(void)err;
	return sd_bus_reply_method_return(m, NULL);
}

static int prop_empty_pixmap(sd_bus *bus, const char *path, const char *iface,
							 const char *prop, sd_bus_message *reply,
							 void *userdata, sd_bus_error *err) {
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	int rc = sd_bus_message_open_container(reply, 'a', "(iiay)");
	if (rc < 0) return rc;
	return sd_bus_message_close_container(reply);
}

static int prop_tooltip(sd_bus *bus, const char *path, const char *iface,
						const char *prop, sd_bus_message *reply,
						void *userdata, sd_bus_error *err) {
	(void)bus;
	(void)path;
	(void)iface;
	(void)prop;
	(void)userdata;
	(void)err;
	int rc = sd_bus_message_open_container(reply, 'r', "sa(iiay)ss");
	if (rc < 0) return rc;
	rc = sd_bus_message_append(reply, "s", "");
	if (rc < 0) return rc;
	rc = sd_bus_message_open_container(reply, 'a', "(iiay)");
	if (rc < 0) return rc;
	rc = sd_bus_message_close_container(reply);
	if (rc < 0) return rc;
	rc = sd_bus_message_append(reply, "ss", "grabit", "recording; click to stop");
	if (rc < 0) return rc;
	return sd_bus_message_close_container(reply);
}

static const sd_bus_vtable item_vtable[] = {
	SD_BUS_VTABLE_START(0),
	SD_BUS_PROPERTY("Category", "s", NULL, offsetof(struct sni_props, category), 0),
	SD_BUS_PROPERTY("Id", "s", NULL, offsetof(struct sni_props, id), 0),
	SD_BUS_PROPERTY("Title", "s", NULL, offsetof(struct sni_props, title), 0),
	SD_BUS_PROPERTY("Status", "s", NULL, offsetof(struct sni_props, status), 0),
	SD_BUS_PROPERTY("WindowId", "u", NULL, offsetof(struct sni_props, window_id), 0),
	SD_BUS_PROPERTY("IconName", "s", NULL, offsetof(struct sni_props, icon_name), 0),
	SD_BUS_PROPERTY("IconPixmap", "a(iiay)", prop_empty_pixmap, 0, 0),
	SD_BUS_PROPERTY("OverlayIconName", "s", NULL, offsetof(struct sni_props, overlay_icon_name), 0),
	SD_BUS_PROPERTY("OverlayIconPixmap", "a(iiay)", prop_empty_pixmap, 0, 0),
	SD_BUS_PROPERTY("AttentionIconName", "s", NULL, offsetof(struct sni_props, attention_icon_name), 0),
	SD_BUS_PROPERTY("AttentionIconPixmap", "a(iiay)", prop_empty_pixmap, 0, 0),
	SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", prop_tooltip, 0, 0),
	SD_BUS_PROPERTY("ItemIsMenu", "b", NULL, offsetof(struct sni_props, item_is_menu), 0),
	SD_BUS_METHOD("Activate", "ii", "", activate_cb, 0),
	SD_BUS_METHOD("SecondaryActivate", "ii", "", activate_cb, 0),
	SD_BUS_METHOD("ContextMenu", "ii", "", activate_cb, 0),
	SD_BUS_METHOD("Scroll", "is", "", noop_cb, 0),
	SD_BUS_VTABLE_END,
};

int sni_run(volatile sig_atomic_t *stop) {
	struct sni_props props = {
		.category = "ApplicationStatus",
		.id = "grabit",
		.title = "grabit",
		.status = "Active",
		.icon_name = "media-record",
		.overlay_icon_name = "",
		.attention_icon_name = "",
		.window_id = 0,
		.item_is_menu = 0,
	};

	sd_bus *bus = NULL;
	int rc = sd_bus_open_user(&bus);
	if (rc < 0) {
		log_warn("tray: no user dbus session (%s)", strerror(-rc));
		notify_tray_unavailable("tray unavailable; no user dbus session");
		return -1;
	}

	char name[64];
	snprintf(name, sizeof name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
	rc = sd_bus_request_name(bus, name, 0);
	if (rc < 0) {
		log_warn("tray: could not claim bus name %s: %s", name, strerror(-rc));
		notify_tray_unavailable("tray unavailable; see terminal for details");
		sd_bus_unref(bus);
		return -1;
	}

	if (sd_bus_add_object_vtable(bus, NULL, ITEM_PATH, ITEM_IFACE,
								 item_vtable, &props) < 0) {
		log_warn("tray: could not register SNI object vtable");
		notify_tray_unavailable("tray unavailable; see terminal for details");
		sd_bus_unref(bus);
		return -1;
	}

	sd_bus_error err = SD_BUS_ERROR_NULL;
	rc = sd_bus_call_method(bus, WATCHER_DEST, WATCHER_PATH, WATCHER_IFACE,
							"RegisterStatusNotifierItem", &err, NULL, "s", name);
	if (rc < 0) {
		const char *ename = err.name ? err.name : "";
		if (strstr(ename, "ServiceUnknown") || strstr(ename, "NameHasNoOwner")) {
			log_warn("tray: no SNI host running (install a status bar with tray support, "
					 "e.g. waybar with tray module; or pass --no-tray to silence)");
			notify_tray_unavailable("no tray host running; see terminal for details");
		} else {
			log_warn("tray: register failed: %s", err.message ? err.message : strerror(-rc));
			notify_tray_unavailable("tray register failed; see terminal for details");
		}
		sd_bus_error_free(&err);
		sd_bus_unref(bus);
		return -1;
	}
	sd_bus_error_free(&err);

	while (!*stop) {
		rc = sd_bus_process(bus, NULL);
		if (rc < 0 && rc != -EINTR) break;
		if (rc > 0) continue;
		rc = sd_bus_wait(bus, (uint64_t)-1);
		if (rc < 0 && rc != -EINTR) break;
	}

	sd_bus_unref(bus);
	return 0;
}
