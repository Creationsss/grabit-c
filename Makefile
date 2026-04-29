VERSION    := 0.1.0
NAME       := grabit

BUILDDIR   := build
PREFIX     ?= /usr/local
DESTDIR    ?=
CC              ?= cc
PKG_CONFIG      ?= pkg-config
WAYLAND_SCANNER ?= wayland-scanner

WARN := \
	-Wall -Wextra -Wpedantic -Wshadow \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wvla -Wformat=2 -Wformat-security \
	-Wnull-dereference -Wpointer-arith

HARDEN := \
	-fstack-protector-strong -fno-plt -fno-common -D_FORTIFY_SOURCE=2

CFLAGS  ?= -O2 -g
CFLAGS  += -std=c17 $(WARN) $(HARDEN) \
           -DGRABIT_VERSION=\"$(VERSION)\" \
           -Isrc \
           -MMD -MP
LDFLAGS ?=
LDLIBS  ?=

HAVE_BASU       := $(shell $(PKG_CONFIG) --exists basu       && echo 1)
HAVE_ELOGIND    := $(shell $(PKG_CONFIG) --exists libelogind  && echo 1)
HAVE_LIBSYSTEMD := $(shell $(PKG_CONFIG) --exists libsystemd  && echo 1)

ifeq ($(HAVE_BASU),1)
BUS_PKG    := basu
BUS_DEFINE := -DGRABIT_BUS_BASU
else ifeq ($(HAVE_ELOGIND),1)
BUS_PKG    := libelogind
BUS_DEFINE := -DGRABIT_BUS_ELOGIND
else ifeq ($(HAVE_LIBSYSTEMD),1)
BUS_PKG    := libsystemd
BUS_DEFINE :=
else
$(warning no sd-bus impl found — install basu-devel (Void / non-systemd), libelogind-devel, or libsystemd-devel)
BUS_PKG    :=
BUS_DEFINE :=
endif

PKGS_CORE := json-c libcurl wayland-client wayland-cursor cairo xkbcommon $(BUS_PKG)
CFLAGS    += $(BUS_DEFINE) $(shell $(PKG_CONFIG) --cflags $(PKGS_CORE)) -pthread
LDLIBS    += $(shell $(PKG_CONFIG) --libs   $(PKGS_CORE)) -lmagic -lrt -pthread

WL_PROTOCOLS := \
	wlr-screencopy-unstable-v1 \
	wlr-data-control-unstable-v1 \
	wlr-layer-shell-unstable-v1 \
	xdg-output-unstable-v1 \
	xdg-shell

WL_PROTO_DIR     := $(BUILDDIR)/protocols
WL_PROTO_HEADERS := $(addprefix $(WL_PROTO_DIR)/,$(addsuffix -client-protocol.h,$(WL_PROTOCOLS)))
WL_PROTO_SRCS    := $(addprefix $(WL_PROTO_DIR)/,$(addsuffix -protocol.c,$(WL_PROTOCOLS)))
WL_PROTO_OBJS    := $(WL_PROTO_SRCS:%.c=%.o)

CFLAGS += -I$(WL_PROTO_DIR)

$(WL_PROTO_DIR)/%-client-protocol.h: protocols/%.xml | $(WL_PROTO_DIR)
	$(WAYLAND_SCANNER) client-header $< $@

$(WL_PROTO_DIR)/%-protocol.c: protocols/%.xml | $(WL_PROTO_DIR)
	$(WAYLAND_SCANNER) private-code $< $@

$(WL_PROTO_DIR):
	@mkdir -p $@

GRABIT_SRCS := \
	src/main.c \
	src/args.c \
	src/log.c \
	src/paths.c \
	src/util.c \
	src/config.c \
	src/template.c \
	src/hyprland.c \
	src/mime.c \
	src/wl.c \
	src/capture/wlr_screencopy.c \
	src/capture/png.c \
	src/clipboard/wlr_data_control.c \
	src/notify/sd_bus.c \
	src/region/wlr_layer.c \
	src/record/record.c \
	src/record/ring.c \
	src/record/ffmpeg.c \
	src/record/pid.c \
	src/record/compose.c \
	src/upload/upload.c

GRABIT_VENDOR_SRCS := \
	src/vendor/tomlc99/toml.c

GRABIT_OBJS := $(GRABIT_SRCS:%.c=$(BUILDDIR)/%.o) \
               $(GRABIT_VENDOR_SRCS:%.c=$(BUILDDIR)/%.o) \
               $(WL_PROTO_OBJS)
GRABIT_BIN  := $(BUILDDIR)/grabit

CHECK_SRCS  := tools/check_headers.c
CHECK_OBJS  := $(CHECK_SRCS:%.c=$(BUILDDIR)/%.o)
CHECK_BIN   := $(BUILDDIR)/check_headers

OBJS := $(GRABIT_OBJS) $(CHECK_OBJS)
DEPS := $(OBJS:.o=.d)

.PHONY: all
all: $(GRABIT_BIN)

$(GRABIT_BIN): $(GRABIT_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(CHECK_BIN): $(CHECK_OBJS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

$(BUILDDIR)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

$(GRABIT_OBJS): | $(WL_PROTO_HEADERS)

$(WL_PROTO_DIR)/%.o: $(WL_PROTO_DIR)/%.c
	$(CC) $(filter-out -Wpedantic -Wmissing-prototypes -Wstrict-prototypes,$(CFLAGS)) -c -o $@ $<

$(BUILDDIR)/src/vendor/%.o: src/vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(filter-out -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wnull-dereference,$(CFLAGS)) -c -o $@ $<

.PHONY: test
test: $(CHECK_BIN)
	$(CHECK_BIN) --check src

.PHONY: apply-headers
apply-headers: $(CHECK_BIN)
	$(CHECK_BIN) --apply src

FMT_SRCS := $(shell find src -path src/vendor -prune -o \( -name '*.c' -o -name '*.h' \) -print) tools/check_headers.c

.PHONY: fmt
fmt:
	clang-format -i $(FMT_SRCS)

.PHONY: fmt-check
fmt-check:
	clang-format --dry-run -Werror $(FMT_SRCS)

SAN_BUILDDIR := build-san
SAN_OBJS     := $(GRABIT_SRCS:%.c=$(SAN_BUILDDIR)/%.o)
SAN_VOBJS    := $(GRABIT_VENDOR_SRCS:%.c=$(SAN_BUILDDIR)/%.o)
SAN_DEPS     := $(SAN_OBJS:.o=.d)
SAN_BIN      := $(SAN_BUILDDIR)/grabit
SAN_FLAGS    := -O0 -g3 -fsanitize=address,undefined -fno-omit-frame-pointer
SAN_CFLAGS    = $(filter-out -D_FORTIFY_SOURCE=2,$(CFLAGS))

$(SAN_BIN): $(SAN_OBJS) $(SAN_VOBJS) $(WL_PROTO_OBJS)
	@mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $(SAN_FLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(SAN_BUILDDIR)/src/%.o: src/%.c
	@mkdir -p $(@D)
	$(CC) $(SAN_CFLAGS) $(SAN_FLAGS) -c -o $@ $<

$(SAN_BUILDDIR)/src/vendor/%.o: src/vendor/%.c
	@mkdir -p $(@D)
	$(CC) $(filter-out -Wpedantic -Wmissing-prototypes -Wstrict-prototypes -Wshadow -Wnull-dereference,$(SAN_CFLAGS)) $(SAN_FLAGS) -c -o $@ $<

$(SAN_OBJS): | $(WL_PROTO_HEADERS)

.PHONY: sanitize
sanitize: $(SAN_BIN)

-include $(SAN_DEPS)

.PHONY: install
install: $(GRABIT_BIN)
	install -Dm755 $(GRABIT_BIN) $(DESTDIR)$(PREFIX)/bin/$(NAME)

.PHONY: clean
clean:
	rm -rf $(BUILDDIR) $(SAN_BUILDDIR)

-include $(DEPS)
