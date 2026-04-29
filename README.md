# grabit

screenshot, screen-recording, ocr, and uploader for wlroots wayland compositors.

works on: hyprland, sway, niri, river.
not supported: x11, kde, gnome.

## features

- region screenshots with native freeze + selector (no `slurp`/`grim` shellouts)
- screen recording to mp4/h.264 with live overlay + sni tray icon
- ocr (capture → text → clipboard) via libtesseract
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

recording (always built; uses ffmpeg as a subprocess at runtime, no link-time dep beyond what's above).

ocr (auto-detected at build time):
- `tesseract` + `leptonica` development headers
- english training data at runtime: `eng.traineddata` under `/usr/share/tessdata/` or `$TESSDATA_PREFIX`

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

## ocr

```sh
grabit --tesseract            # select a region; text lands in clipboard
```

requires `eng.traineddata` at runtime (in `/usr/share/tessdata/` or `$TESSDATA_PREFIX`). if grabit was built without tesseract development headers, `--tesseract` will tell you so.

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
| `XDG_RUNTIME_DIR` | recording pid file lives here if set, else `/tmp` |
| `XDG_VIDEOS_DIR` | recording save dir (overridden by `save_dir` config; else `~/Videos`) |
| `TESSDATA_PREFIX` | tesseract language-data dir |

## license

agpl-3.0-or-later. see `LICENSE`.
