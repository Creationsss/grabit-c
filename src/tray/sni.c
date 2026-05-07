// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 creations

#define _XOPEN_SOURCE 700
#include "tray/sni.h"

#include "log.h"
#include "notify/notify.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <dbus/dbus.h>

#define WATCHER_DEST "org.kde.StatusNotifierWatcher"
#define WATCHER_PATH "/StatusNotifierWatcher"
#define WATCHER_IFACE "org.kde.StatusNotifierWatcher"

#define ITEM_PATH "/StatusNotifierItem"
#define ITEM_IFACE "org.kde.StatusNotifierItem"

#define IFACE_PROPS "org.freedesktop.DBus.Properties"
#define IFACE_INTROSPECT "org.freedesktop.DBus.Introspectable"

struct sni_props {
	const char *category;
	const char *id;
	const char *title;
	const char *status;
	const char *icon_name;
	const char *overlay_icon_name;
	const char *attention_icon_name;
	dbus_uint32_t window_id;
	dbus_bool_t item_is_menu;
};

static const char INTROSPECT_XML[] =
	"<!DOCTYPE node PUBLIC \"-//freedesktop//DTD D-BUS Object Introspection 1.0//EN\""
	" \"http://www.freedesktop.org/standards/dbus/1.0/introspect.dtd\">\n"
	"<node>"
	" <interface name=\"" IFACE_INTROSPECT "\">"
	"  <method name=\"Introspect\"><arg name=\"data\" type=\"s\" direction=\"out\"/></method>"
	" </interface>"
	" <interface name=\"" IFACE_PROPS "\">"
	"  <method name=\"Get\">"
	"   <arg name=\"interface\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"property\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"value\" type=\"v\" direction=\"out\"/>"
	"  </method>"
	"  <method name=\"GetAll\">"
	"   <arg name=\"interface\" type=\"s\" direction=\"in\"/>"
	"   <arg name=\"props\" type=\"a{sv}\" direction=\"out\"/>"
	"  </method>"
	" </interface>"
	" <interface name=\"" ITEM_IFACE "\">"
	"  <property name=\"Category\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Id\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Title\" type=\"s\" access=\"read\"/>"
	"  <property name=\"Status\" type=\"s\" access=\"read\"/>"
	"  <property name=\"IconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"IconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"OverlayIconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"OverlayIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"AttentionIconName\" type=\"s\" access=\"read\"/>"
	"  <property name=\"AttentionIconPixmap\" type=\"a(iiay)\" access=\"read\"/>"
	"  <property name=\"ToolTip\" type=\"(sa(iiay)ss)\" access=\"read\"/>"
	"  <property name=\"ItemIsMenu\" type=\"b\" access=\"read\"/>"
	"  <property name=\"WindowId\" type=\"u\" access=\"read\"/>"
	"  <method name=\"Activate\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"SecondaryActivate\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"ContextMenu\"><arg type=\"i\"/><arg type=\"i\"/></method>"
	"  <method name=\"Scroll\"><arg type=\"i\"/><arg type=\"s\"/></method>"
	" </interface>"
	"</node>";

static void notify_tray_unavailable(const char *body) {
	notify_send(&(struct notify_opts){
		.summary = "grabit: setup needed",
		.body = body,
		.force = true,
	});
}

static void send_empty_reply(DBusConnection *bus, DBusMessage *msg) {
	DBusMessage *r = dbus_message_new_method_return(msg);
	if (!r) return;
	dbus_connection_send(bus, r, NULL);
	dbus_message_unref(r);
}

static void append_variant_str(DBusMessageIter *parent, const char *s) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "s", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_STRING, &s);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_uint32(DBusMessageIter *parent, dbus_uint32_t u) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "u", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_UINT32, &u);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_bool(DBusMessageIter *parent, dbus_bool_t b) {
	DBusMessageIter v;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "b", &v);
	dbus_message_iter_append_basic(&v, DBUS_TYPE_BOOLEAN, &b);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_empty_pixmap(DBusMessageIter *parent) {
	DBusMessageIter v, a;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "a(iiay)", &v);
	dbus_message_iter_open_container(&v, DBUS_TYPE_ARRAY, "(iiay)", &a);
	dbus_message_iter_close_container(&v, &a);
	dbus_message_iter_close_container(parent, &v);
}

static void append_variant_tooltip(DBusMessageIter *parent) {
	DBusMessageIter v, st, pixmap;
	dbus_message_iter_open_container(parent, DBUS_TYPE_VARIANT, "(sa(iiay)ss)", &v);
	dbus_message_iter_open_container(&v, DBUS_TYPE_STRUCT, NULL, &st);
	const char *empty = "";
	const char *name = "grabit";
	const char *body = "recording; click to stop";
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &empty);
	dbus_message_iter_open_container(&st, DBUS_TYPE_ARRAY, "(iiay)", &pixmap);
	dbus_message_iter_close_container(&st, &pixmap);
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &name);
	dbus_message_iter_append_basic(&st, DBUS_TYPE_STRING, &body);
	dbus_message_iter_close_container(&v, &st);
	dbus_message_iter_close_container(parent, &v);
}

static bool append_property_value(DBusMessageIter *parent, const char *name,
								  const struct sni_props *p) {
	if (strcmp(name, "Category") == 0) {
		append_variant_str(parent, p->category);
		return true;
	}
	if (strcmp(name, "Id") == 0) {
		append_variant_str(parent, p->id);
		return true;
	}
	if (strcmp(name, "Title") == 0) {
		append_variant_str(parent, p->title);
		return true;
	}
	if (strcmp(name, "Status") == 0) {
		append_variant_str(parent, p->status);
		return true;
	}
	if (strcmp(name, "IconName") == 0) {
		append_variant_str(parent, p->icon_name);
		return true;
	}
	if (strcmp(name, "OverlayIconName") == 0) {
		append_variant_str(parent, p->overlay_icon_name);
		return true;
	}
	if (strcmp(name, "AttentionIconName") == 0) {
		append_variant_str(parent, p->attention_icon_name);
		return true;
	}
	if (strcmp(name, "WindowId") == 0) {
		append_variant_uint32(parent, p->window_id);
		return true;
	}
	if (strcmp(name, "ItemIsMenu") == 0) {
		append_variant_bool(parent, p->item_is_menu);
		return true;
	}
	if (strcmp(name, "IconPixmap") == 0 || strcmp(name, "OverlayIconPixmap") == 0 ||
		strcmp(name, "AttentionIconPixmap") == 0) {
		append_variant_empty_pixmap(parent);
		return true;
	}
	if (strcmp(name, "ToolTip") == 0) {
		append_variant_tooltip(parent);
		return true;
	}
	return false;
}

static const char *const ALL_PROP_NAMES[] = {
	"Category",
	"Id",
	"Title",
	"Status",
	"IconName",
	"IconPixmap",
	"OverlayIconName",
	"OverlayIconPixmap",
	"AttentionIconName",
	"AttentionIconPixmap",
	"ToolTip",
	"ItemIsMenu",
	"WindowId",
	NULL,
};

static DBusHandlerResult handle_get(DBusConnection *bus, DBusMessage *msg,
									const struct sni_props *p) {
	const char *iface = NULL, *prop = NULL;
	if (!dbus_message_get_args(msg, NULL,
							   DBUS_TYPE_STRING, &iface,
							   DBUS_TYPE_STRING, &prop,
							   DBUS_TYPE_INVALID)) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (strcmp(iface, ITEM_IFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter args;
	dbus_message_iter_init_append(reply, &args);
	if (!append_property_value(&args, prop, p)) {
		dbus_message_unref(reply);
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	dbus_connection_send(bus, reply, NULL);
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_get_all(DBusConnection *bus, DBusMessage *msg,
										const struct sni_props *p) {
	const char *iface = NULL;
	if (!dbus_message_get_args(msg, NULL, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID)) {
		return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
	}
	if (strcmp(iface, ITEM_IFACE) != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	DBusMessageIter args, dict;
	dbus_message_iter_init_append(reply, &args);
	dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
	for (size_t i = 0; ALL_PROP_NAMES[i]; i++) {
		DBusMessageIter entry;
		dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
		dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &ALL_PROP_NAMES[i]);
		append_property_value(&entry, ALL_PROP_NAMES[i], p);
		dbus_message_iter_close_container(&dict, &entry);
	}
	dbus_message_iter_close_container(&args, &dict);
	dbus_connection_send(bus, reply, NULL);
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_introspect(DBusConnection *bus, DBusMessage *msg) {
	DBusMessage *reply = dbus_message_new_method_return(msg);
	if (!reply) return DBUS_HANDLER_RESULT_NEED_MEMORY;
	const char *xml = INTROSPECT_XML;
	dbus_message_append_args(reply, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID);
	dbus_connection_send(bus, reply, NULL);
	dbus_message_unref(reply);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult handle_activate(DBusConnection *bus, DBusMessage *msg) {
	pid_t parent = getppid();
	if (parent > 1) kill(parent, SIGINT);
	send_empty_reply(bus, msg);
	return DBUS_HANDLER_RESULT_HANDLED;
}

static DBusHandlerResult sni_handler(DBusConnection *bus, DBusMessage *msg, void *data) {
	const struct sni_props *p = data;
	const char *iface = dbus_message_get_interface(msg);
	const char *member = dbus_message_get_member(msg);
	if (!iface || !member) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

	if (strcmp(iface, IFACE_PROPS) == 0) {
		if (strcmp(member, "Get") == 0) return handle_get(bus, msg, p);
		if (strcmp(member, "GetAll") == 0) return handle_get_all(bus, msg, p);
	}
	if (strcmp(iface, IFACE_INTROSPECT) == 0 && strcmp(member, "Introspect") == 0) {
		return handle_introspect(bus, msg);
	}
	if (strcmp(iface, ITEM_IFACE) == 0) {
		if (strcmp(member, "Activate") == 0 ||
			strcmp(member, "SecondaryActivate") == 0 ||
			strcmp(member, "ContextMenu") == 0) return handle_activate(bus, msg);
		if (strcmp(member, "Scroll") == 0) {
			send_empty_reply(bus, msg);
			return DBUS_HANDLER_RESULT_HANDLED;
		}
	}
	return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const DBusObjectPathVTable item_vtable = {
	.unregister_function = NULL,
	.message_function = sni_handler,
};

int sni_run(volatile sig_atomic_t *stop) {
	static struct sni_props props = {
		.category = "ApplicationStatus",
		.id = "grabit",
		.title = "grabit",
		.status = "Active",
		.icon_name = "media-record",
		.overlay_icon_name = "",
		.attention_icon_name = "",
		.window_id = 0,
		.item_is_menu = FALSE,
	};

	DBusError err;
	dbus_error_init(&err);

	DBusConnection *bus = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
	if (!bus) {
		log_warn("tray: no user dbus session (%s)", err.message ? err.message : "unknown");
		notify_tray_unavailable("tray unavailable; no user dbus session");
		dbus_error_free(&err);
		return -1;
	}
	dbus_connection_set_exit_on_disconnect(bus, FALSE);

	char name[64];
	snprintf(name, sizeof name, "org.kde.StatusNotifierItem-%d-1", (int)getpid());
	int rc = dbus_bus_request_name(bus, name, 0, &err);
	if (rc != DBUS_REQUEST_NAME_REPLY_PRIMARY_OWNER) {
		log_warn("tray: could not claim bus name %s: %s", name,
				 err.message ? err.message : "unknown");
		notify_tray_unavailable("tray unavailable; see terminal for details");
		dbus_error_free(&err);
		dbus_connection_close(bus);
		dbus_connection_unref(bus);
		return -1;
	}

	if (!dbus_connection_register_object_path(bus, ITEM_PATH, &item_vtable, &props)) {
		log_warn("tray: could not register SNI object path");
		notify_tray_unavailable("tray unavailable; see terminal for details");
		dbus_connection_close(bus);
		dbus_connection_unref(bus);
		dbus_error_free(&err);
		return -1;
	}

	DBusMessage *reg = dbus_message_new_method_call(WATCHER_DEST, WATCHER_PATH,
													WATCHER_IFACE,
													"RegisterStatusNotifierItem");
	if (!reg) {
		log_warn("tray: oom building register call");
		dbus_connection_close(bus);
		dbus_connection_unref(bus);
		dbus_error_free(&err);
		return -1;
	}
	const char *name_arg = name;
	dbus_message_append_args(reg, DBUS_TYPE_STRING, &name_arg, DBUS_TYPE_INVALID);

	DBusMessage *reply = dbus_connection_send_with_reply_and_block(bus, reg, 2000, &err);
	dbus_message_unref(reg);
	if (!reply) {
		const char *ename = err.name ? err.name : "";
		if (strstr(ename, "ServiceUnknown") || strstr(ename, "NameHasNoOwner")) {
			log_warn("tray: no SNI host running (install a status bar with tray support, "
					 "e.g. waybar with tray module; or pass --no-tray to silence)");
			notify_tray_unavailable("no tray host running; see terminal for details");
		} else {
			log_warn("tray: register failed: %s",
					 err.message ? err.message : "unknown");
			notify_tray_unavailable("tray register failed; see terminal for details");
		}
		dbus_error_free(&err);
		dbus_connection_close(bus);
		dbus_connection_unref(bus);
		return -1;
	}
	dbus_message_unref(reply);
	dbus_error_free(&err);

	while (!*stop) {
		if (!dbus_connection_read_write_dispatch(bus, 100)) break;
	}

	dbus_connection_close(bus);
	dbus_connection_unref(bus);
	return 0;
}
