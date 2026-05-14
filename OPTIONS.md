# grabit options

everything beyond the at-a-glance README. see `README.md` for install.

## usage

```sh
grabit -c                     # region screenshot → clipboard
grabit -u                     # upload to default service
grabit -o > path.txt          # save and print path
grabit --record               # toggle recording (run again to stop)
grabit --pin                  # pin a region screenshot to the desktop
grabit --tesseract            # ocr a region → clipboard
grabit -e -c                  # annotate before copying
grabit -f file.png -u         # upload an existing file
```

first run writes a default config to `~/.config/grabit/config.toml`.

## features

- region screenshots with native freeze + selector (no `slurp`/`grim` shellouts)
- screen recording to mp4/h.264 with live overlay + sni tray icon
- ocr (capture → text → clipboard) via tesseract
- in-tree annotation editor (`--edit`): pen, rect, ellipse, arrow, blur, text, eraser, hsl color picker + eyedropper, hex input
- pin captures to the desktop (always-on-top, click-through, draggable when grabbed)
- six built-in uploaders: zipline, nest, fakecrime, ez, guns, pixelvault
- import any sharex `.sxcu` uploader
- filename templates
- toml config + `grabit set/get/unset` schema-validated cli
- plugin system (`grabit plugin install <git-url>`)

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
- wayland-client + wayland-cursor
- cairo
- libxkbcommon
- libdbus-1

build-time only (data, no link): `wayland-protocols`, `wayland-scanner`.

optional (auto-detected via pkg-config):
- `libjpeg` (or `libjpeg-turbo`) — enables JPEG output (`format = jpeg`).
- `libwebp` — enables WebP output (`format = webp`).

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

### zipline custom headers

zipline supports per-upload metadata via headers. set them with `services.zipline.headers.<name>`:

| header | accepted values |
|---|---|
| `x-zipline-format` | `random`, `date`, `uuid`, `name`, `gfycat` — **defaults to `name`** so the uploaded URL preserves grabit's filename template (e.g. `%w`, `%Y-%m-%d`). Set it explicitly to override. |
| `x-zipline-image-compression-percent` | 0-100 |
| `x-zipline-image-compression-type` | `jpg`, `png`, `webp`, `jxl` |
| `x-zipline-password` | string |
| `x-zipline-max-views` | non-negative integer |
| `x-zipline-no-json` | `true` |
| `x-zipline-original-name` | `true` |
| `x-zipline-folder` | folder id |
| `x-zipline-filename` | string |
| `x-zipline-domain` | string |
| `x-zipline-file-extension` | string |
| `x-zipline-deletes-at` | duration string (e.g. `1d`, `30m`) |

unknown header names are forwarded as-is with a warning.

### sharex (.sxcu) uploaders

import any sharex custom uploader file:

```sh
grabit sxcu add  ~/Downloads/myhost.sxcu     # parse, sanitize name, copy into config dir
grabit sxcu list                              # registered names (alias: ls)
grabit sxcu show <name>                       # parsed fields (url, method, headers, ...)
grabit sxcu remove <name>                     # alias: rm
```

added uploaders live at `~/.config/grabit/uploaders/<name>.sxcu` (chmod 0600). once added, use them like a built-in:

```sh
grabit --<name>                               # screenshot + upload
grabit -f file.png --<name>                   # upload an existing file
```

supported sxcu fields: `RequestURL`, `RequestMethod`, `Body` (`MultipartFormData`/`FormURLEncoded`/`JSON`/`XML`/`Binary`/`None`), `FileFormName`, `Headers`, `Parameters`, `Arguments`, `Data`, `URL`, `ErrorMessage`, `RegexList`, `DestinationType`.

placeholders in url/headers/args/data: `{filename}`, `{base64:...}`, `{random:a|b|c}`, `{select:a|b|c}`, `{prompt:label|default}` (alias `{inputbox:label|default}`). response placeholders for the `URL`/`ErrorMessage` templates: `{response}`, `{responseurl}`, `{json:path.to[0].field}`, `{regex:pattern|group}`, `{regex:N|group}` (N indexes `RegexList`), `{header:Name}`.

auth lives inside the `.sxcu` `Headers` block — no separate `services.<name>.auth` config needed.

### default action

if no `-c`/`-u`/`-o`/`--<service>` is passed, falls back to:

```sh
grabit set default_action copy        # one of: copy, upload, save, pin
```

### top-level keys

| key | type | notes |
|---|---|---|
| `default_action` | enum | `copy`/`upload`/`save`/`pin` (default `copy`) |
| `service` | string | default upload target when `default_action=upload` (one of the built-ins or an sxcu name) |
| `notifications` | bool | enable desktop notifications (default `true`) |
| `also_save` | bool | also save a copy when copying/uploading (default `false`). Alias: `save_captures` (legacy). |
| `save_dir` | string | save dir for screenshots and recordings (overrides `XDG_PICTURES_DIR`/`XDG_VIDEOS_DIR`; default `~/Pictures` for screenshots, `~/Videos` for videos) |
| `editor` | string | external editor binary for `-e` text tool (optional) |
| `filename` | string | filename template (see "filename templates" below) |
| `filename_preset` | enum | `date`/`random`/`uuid`/`timestamp` |
| `format` | enum | screenshot output format: `png`/`jpeg`/`webp` (default `png`). per-run override: `--format <name>` |

### encoder options

| key | default | notes |
|---|---|---|
| `jpeg.quality` | `90` | JPEG quality 1–100 |
| `webp.quality` | `85` | WebP quality 0–100 (ignored when `webp.lossless = true`) |
| `webp.lossless` | `false` | use WebP lossless mode |

JPEG and WebP support is detected at build time via `pkg-config libjpeg` and `pkg-config libwebp`. If a format wasn't compiled in, picking it at runtime errors out clearly. PNG is always available.

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
- `grabit --record --no-upload`: skip auto-upload even if `default_action=upload`
- `grabit --record --zipline`: upload to a specific service after recording

config keys (all optional):

| key | default | notes |
|---|---|---|
| `recording.fps` | 30 | 1-120 |
| `recording.crf` | 23 | 0-51 (lower = higher quality) |
| `recording.preset` | `fast` | one of: `ultrafast`, `superfast`, `veryfast`, `faster`, `fast`, `medium`, `slow`, `slower`, `veryslow` |
| `recording.tune` | (none) | one of: `film`, `animation`, `grain`, `stillimage`, `psnr`, `ssim`, `fastdecode`, `zerolatency` |
| `recording.pix_fmt` | `yuv420p` | one of: `yuv420p`, `yuv422p`, `yuv444p`, `yuv420p10le` |
| `recording.cursor` | `true` | record the cursor |
| `recording.max_size_mb` | (none) | re-encode if file exceeds this (0-100000) |
| `recording.ffmpeg` | `ffmpeg` | path to ffmpeg binary |

## sound

play a shutter sound on capture (off by default):

| key | default | notes |
|---|---|---|
| `sound.enabled` | `false` | toggle |
| `sound.player` | (auto) | path to player binary (auto-detects `pw-play`, `paplay`, `play`, `aplay`) |
| `sound.file` | (auto) | path to audio file (auto-detects standard freedesktop camera-shutter sounds) |

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
- **width slider** (1–12 in the toolbar; the persisted `edit.width` accepts up to 20 if you set it via the cli)
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
| `%%` | literal `%` |

presets via `filename_preset`:
- `date`: `%Y-%m-%d-%H-%M-%S` (default)
- `random`: `%r12`
- `uuid`: `%u`
- `timestamp`: `%s`

`%w`/`%t` resolve to empty on non-hyprland compositors.

## flags

action flags:

| flag | meaning |
|---|---|
| `-c` | copy to clipboard |
| `-u` | upload to default service |
| `-o` / `--output` / `--save` | save and print path |
| `--<service>` | upload to a specific built-in or sxcu service |
| `--record` | toggle recording |
| `--pin` | pin a region to the desktop |
| `--grab` / `--release` / `--close-all` | manage existing pins |
| `--tesseract` | ocr a region into the clipboard |
| `-e` / `--edit` | annotate before the action |

modifiers:

| flag | meaning |
|---|---|
| `-f <file>` | use an existing file instead of capturing |
| `--filename <tpl>` (or `--filename=<tpl>`) | per-run filename template |
| `--format <png\|jpeg\|webp>` (or `--format=<name>`) | per-run output format |
| `--no-tray` | suppress the recording tray icon |
| `--no-upload` | with `--record`, skip the auto-upload after recording |
| `--silent` / `-q` / `--quiet` | suppress info logging (errors still print) |
| `-d` / `--debug` | enable debug logging |

## environment

| var | effect |
|---|---|
| `GRABIT_DEBUG=1` | enable debug logging (same as `-d`) |
| `GRABIT_<SERVICE>_AUTH` | per-service auth token (overrides config) |
| `XDG_RUNTIME_DIR` | recording pid file + per-pin ipc sockets live here if set, else `/tmp` |
| `XDG_PICTURES_DIR` | screenshot save dir (overridden by `save_dir` config; else `~/Pictures`) |
| `XDG_VIDEOS_DIR` | recording save dir (overridden by `save_dir` config; else `~/Videos`) |
| `TESSDATA_PREFIX` | tesseract language-data dir |
