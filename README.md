# cheder

A small Qt6 image viewer with a thumbnail grid, a single-image view (with
animated-GIF playback), and a text-driven action bar for editing and
organizing images.

## Build

Requires Qt 6 (Widgets) and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build -j
```

Optional external tools:

- [`exiftool`](https://exiftool.org) — the info panel uses it for EXIF
  fields (camera/lens/exposure); without it only the basics are shown.
- `ffmpeg` — required by the **Animation** action (GIF/MP4 encoding).

## Run

```sh
./build/cheder              # prompts for file or directory
./build/cheder /path/dir/   # opens directory in thumbnail view
./build/cheder image.jpg    # opens that image, scoped to its parent dir
```

## Shortcuts

### Thumbnail view

- `Tab` / `Enter` — open first selected image in image view
- arrow keys / `h j k l` — move within the grid
- `g g` / `G` — first / last item
- `+` / `-` / `Ctrl+wheel` — change thumbnail size

### Image view

- arrow keys / `n` / `N` — next / previous image
- `i` — toggle the info panel
- `Tab` / `Esc` — back to the thumbnail view

### Anywhere

- `Ctrl+P` — focus the action bar (type to filter, `Enter` runs, `Esc`/`Tab` dismiss)
- `Ctrl+C` — copy the current selection to the clipboard (file URL + pixels for a single image; URLs only for many)
- `F9` — copy the current selection's filepath(s) to the clipboard as plain text (one per line — paste-anywhere; for file-reference clipboard use `Ctrl+C`)
- `m` — send the current selection's filepath(s) to [`dungeon`](https://github.com/pglira/dungeon) (external helper, must be on `PATH`)
- `Delete` / `d d` — move the current selection to trash (with confirmation)
- `F5` — re-scan the source directory
- `q` — quit

## Actions

The action bar at the bottom of the window lists actions; type to filter,
`Enter` to invoke. Built-in:

- **Animation** (`Alt+A`) — build an animated GIF or H.264 MP4 from ≥2 same-size frames; FPS, loop count (GIF), quality (MP4), live frame-flip preview
- **Annotate** (`Alt+D`) — draw rectangles, circles, arrows, and text on a single image with per-shape color / stroke / font; select-and-edit with handles, double-click to retype text, `Ctrl+Z` to undo
- **Caption** (`Alt+T`) — add a text caption above or below a single image (live preview)
- **Concatenate** (`Alt+N`) — stitch ≥2 selected images horizontally or vertically (target axis, spacing, background)
- **Copy or move** (`Alt+M`) — duplicate or relocate to a chosen directory
- **Crop** (`Alt+C`) — interactive rectangle on a preview, with optional aspect-ratio constraint; applies the same rect to every selected image (selection must be same-size)
- **Grid** (`Alt+G`) — compose ≥2 selected images into a 2D grid with optional per-cell titles, font, spacing, and background color (live preview)
- **Rename** (`F2`) — rename a single image; output directory editable too, so it doubles as "rename + move"
- **Resize** (`Alt+S`) — by longest edge (px) or scale (%)
- **Rotate** (`Alt+R`) — 90° CW / CCW / 180°

Each shortcut opens the corresponding action's dialog directly from any view.
Selections that the action can't accept (e.g. `Alt+N` on a single image) fall
back to a status-bar message instead of opening.

Each action writes to a directory you pick in its dialog (defaults to
`<source-dir>/<action-id>/`). After a successful run the source directory is
re-scanned automatically.

## Cache

Thumbnails are cached at `~/.cache/cheder/thumbs/` as PNGs keyed by the SHA-1
of the absolute source path. The source mtime is stored in PNG metadata, so
edits invalidate cached entries transparently.

## License

MIT — see [LICENSE](LICENSE).
