# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river.
not supported: x11, kde, gnome.

## features

- region screenshots with native freeze + selector (no `slurp`/`grim` shellouts)
- screen recording to mp4/h.264 with live overlay + sni tray icon
- ocr (capture → text → clipboard) via tesseract
- in-tree annotation editor (`--edit`): pen, rect, ellipse, arrow, blur, text, eraser, hsl color picker + eyedropper, hex input
- pin captures to the desktop (always-on-top, click-through, draggable when grabbed)
- six built-in uploaders: zipline, nest, fakecrime, ez, guns, pixelvault
- filename templates (`%Y-%m-%d-%H-%M-%S`, `%w` window class, `%t` window title, `%r` random, `%u` uuid, `%s` unix ts, `%o` output name)
- toml config + `grabit set/get/unset` schema-validated cli
- runs on non-systemd distros (basu/elogind auto-detected)

## build

```sh
make
./build/grabit --version
```

### deps

required:
- json-c
- libcurl
- libmagic
- wayland-client + wayland-protocols
- cairo
- libxkbcommon
- sd-bus impl: `basu`, `libelogind`, or `libsystemd` (auto-detected by pkg-config)

runtime (no link-time deps, looked up via `$PATH`):
- `ffmpeg` for `--record`
- `tesseract` + the english training data for `--tesseract`

config parser (`tomlc99`) is vendored under `src/vendor/`.

### targets

```sh
make             # release build into build/grabit
make sanitize    # asan + ubsan into build-san/grabit
make install     # to $(DESTDIR)$(PREFIX)/bin/grabit
make clean
make test        # spdx-header lint
make apply-headers
make fmt         # clang-format -i
make fmt-check   # dry-run, errors on diff
```

### development build

generate `compile_commands.json` for clangd / your editor's lsp:

```sh
bear -- make all
```

re-run after adding/removing source files.

## quick start

first run writes a sensible default config to `~/.config/grabit/config.toml` and prints the keys you'll likely want next.

```sh
grabit -c                     # copy region screenshot to clipboard
grabit -u                     # upload to default service
grabit --zipline              # upload to a specific service
grabit -o > /tmp/path.txt     # save and print path
grabit --tesseract            # ocr a region, text into clipboard
grabit --record               # toggle recording (run again to stop)
grabit --pin                  # pin a region screenshot to the desktop
grabit -e -c                  # annotate before copying (-e pairs with -c/-u/-o)
grabit -f path.png -u         # upload an existing file
```

## configuration

```sh
grabit set                    # list all settable keys
grabit set <key>              # show example/default for that key
grabit set <key> <value>      # write (validated)
grabit get                    # dump current config
grabit get <key>              # one key
grabit unset <key>
```

config lives at `$XDG_CONFIG_HOME/grabit/config.toml` (else `~/.config/grabit/config.toml`).

### auth tokens

per service, either:

```sh
grabit set services.zipline.auth "<token>"             # plaintext in config (chmod 0600)
```

or via env (preferred, works with password managers):

```sh
export GRABIT_ZIPLINE_AUTH="$(pass show grabit/zipline)"
```

zipline also needs:

```sh
grabit set services.zipline.domain https://your.host
# /api/upload is appended automatically if missing
```

nest accepts an optional `services.nest.folder` (uuid) to upload into a specific folder.

### default action

if no `-c`/`-u`/`-o`/`--<service>` is passed, falls back to:

```sh
grabit set default_action copy        # or upload, save
```

## recording

```sh
grabit --record               # start (region selector, then begins)
grabit --record               # stop
```

while recording you'll see:
- a thin red border around the captured region
- a recording icon in your status bar tray (waybar with `tray` module, etc.)

clicking the tray icon stops the recording.

per-recording overrides:
- `grabit --record --save`: skip auto-upload even if `default_action=upload`
- `grabit --record --zipline`: upload to a specific service after recording

config keys (all optional):

| key | default | notes |
|---|---|---|
| `recording.fps` | 30 | 1-120 |
| `recording.crf` | 20 | 0-51 (lower = higher quality) |
| `recording.preset` | `fast` | x264 preset |
| `recording.tune` | (none) | x264 tune |
| `recording.pix_fmt` | `yuv420p` | output pixel format |
| `recording.cursor` | `true` | record the cursor |
| `recording.max_size_mb` | (none) | re-encode if file exceeds this |
| `recording.ffmpeg` | `ffmpeg` | path to ffmpeg binary |

## pin

```sh
grabit --pin                  # capture a region; pins it to the desktop where it was grabbed
grabit --grab                 # all pins become interactive (X close button + draggable)
grabit --release              # pins go back to click-through
grabit --close-all            # dismiss every pin
```

each pin is a long-lived process holding a wlr-layer-shell overlay surface. they stack as you create them, are click-through by default, and ignore other layers' exclusive zones (so the position matches exactly where the region was selected, even with status bars).

interactive mode is meant to be wired to a hold-bind in your compositor. example for hyprland:

```
bindrn = SUPER SHIFT, mouse:272, exec, grabit --grab
bindrn = SUPER SHIFT, mouse:272, release, exec, grabit --release
```

while grabbed, click anywhere to drag, click the X in the top-right to close that pin.

requires `zwp_relative_pointer_manager_v1` for drag (universal in modern wlroots compositors).

## ocr

```sh
grabit --tesseract            # select a region; text lands in clipboard
```

requires `tesseract` on `$PATH` and `eng.traineddata` (typically in `/usr/share/tessdata/` or `$TESSDATA_PREFIX`). override the binary with `grabit set ocr.tesseract /custom/path/tesseract`.

## edit

```sh
grabit -e -c                  # annotate, then copy
grabit -e -u                  # annotate, then upload
grabit -e -o                  # annotate, then save
```

`-e`/`--edit` pairs with any action. flow: drag a region, then a flameshot-style toolbar appears. tools:

- **pen, rect, ellipse, arrow, blur, text, eraser** — keyboard shortcuts `1`–`7`
- **6 preset color swatches** + a current-color square (click to open the picker)
- **hsl picker panel**: drag in the gradient, type a hex value (`#rrggbb` or `#rgb`), or click the eyedropper to sample a pixel from the screen
- **width slider** (1–12)
- **undo** (`u`, hold to repeat) / **save** (`enter`) / **cancel** (`esc` or right-click)
- **resize handles** on the locked region; **ctrl+drag** inside to move the whole region
- **shift** while drawing constrains rect/ellipse/blur to squares and arrows to 45° angles

last-picked color and width persist via:

| key | default | notes |
|---|---|---|
| `edit.color` | `#ff3030` | `#rrggbb`, `#rgb`, or one of red/yellow/green/blue/black/white |
| `edit.width` | `4` | integer 1–20 |

## filename templates

`grabit --filename '<tpl>'` (per-run) overrides `filename` config (per-user). tokens:

| token | expands to |
|---|---|
| `%Y %m %d %H %M %S` | date/time fields |
| `%s` | unix timestamp |
| `%r` | 12-char random alnum (`%r8` etc. picks length) |
| `%u` | uuid v4 |
| `%w` | active window class (hyprland ipc) |
| `%t` | active window title (hyprland ipc) |
| `%o` | output name where the capture happened |
| `%%` | literal `%` |

presets via `filename_preset`:
- `date`: `%Y-%m-%d-%H-%M-%S` (default)
- `random`: `%r12`
- `uuid`: `%u`
- `timestamp`: `%s`

`%w`/`%t` resolve to empty on non-hyprland compositors.

## environment

| var | effect |
|---|---|
| `GRABIT_DEBUG=1` | enable debug logging (same as `-d`) |
| `GRABIT_<SERVICE>_AUTH` | per-service auth token (overrides config) |
| `XDG_RUNTIME_DIR` | recording pid file + per-pin ipc sockets live here if set, else `/tmp` |
| `XDG_VIDEOS_DIR` | recording save dir (overridden by `save_dir` config; else `~/Videos`) |
| `TESSDATA_PREFIX` | tesseract language-data dir |

## license

agpl-3.0-or-later. see `LICENSE`.
