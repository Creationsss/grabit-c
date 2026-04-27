# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

## status: pre-0.1.0.

works on: hyprland, sway, niri, river.
not supported: x11, kde, gnome.

## build

```sh
make
./build/grabit --version
```

### deps

- sd-bus (`basu`, `libelogind`, or `libsystemd` — auto-detected)
- json-c
- libcurl
- libmagic
- wayland-client + wayland-protocols
- cairo
- libxkbcommon

`basu` is preferred on non-systemd distros (void, alpine, artix, devuan, gentoo-openrc).

### targets

```sh
make sanitize    # asan + ubsan into build-san/
make install     # to $(DESTDIR)$(PREFIX)/bin/grabit
make clean
make test        # spdx header check
make apply-headers
```

## todo

- recording (`--record`) via wlr-screencopy + libav
- ocr (`--tesseract`) via libtesseract
- hyprland ipc for `%w` / `%t` filename tokens
- sni tray (`--no-tray`)
- `--kill`

## license

agpl-3.0-or-later. see `LICENSE`.
