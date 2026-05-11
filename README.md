# cheder

A small Qt6 image viewer with a thumbnail grid, a single-image view, and a
text-driven action bar for batch operations (rotate, resize, copy/move).

## Build

Requires Qt 6 (Widgets) and a C++17 compiler.

```sh
cmake -S . -B build
cmake --build build -j
```

The thumbnail info panel uses [`exiftool`](https://exiftool.org) for EXIF
fields — install it for camera/lens/exposure metadata, otherwise only the
basics are shown.

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

- arrow keys / `n` / `p` — next / previous image
- `i` — toggle the info panel
- `Tab` / `Esc` — back to the thumbnail view

### Anywhere

- `Ctrl+P` — focus the action bar (type to filter, `Enter` runs, `Esc`/`Tab` dismiss)
- `Ctrl+C` — copy the current selection to the clipboard (file URL + pixels for a single image; URLs only for many)
- `F9` — copy the current selection's filepath(s) to the clipboard as plain text (one per line — paste-anywhere; for file-reference clipboard use `Ctrl+C`)
- `Delete` / `d d` — move the current selection to trash (with confirmation)
- `F5` — re-scan the source directory
- `q` — quit

## Actions

The action bar at the bottom of the window lists actions; type to filter,
`Enter` to invoke. Built-in:

- **Rotate** (`Alt+R`) — 90° CW / CCW / 180°
- **Resize** (`Alt+S`) — by longest edge (px) or scale (%)
- **Caption** (`Alt+T`) — add a text caption above or below a single image (live preview)
- **Concatenate** (`Alt+N`) — stitch ≥2 selected images horizontally or vertically (target axis, spacing, background)
- **Crop** (`Alt+C`) — interactive rectangle on a preview, with optional aspect-ratio constraint; applies the same rect to every selected image (selection must be same-size)
- **Rename** (`F2`) — rename a single image; output directory editable too, so it doubles as "rename + move"
- **Copy or move** (`Alt+M`) — duplicate or relocate to a chosen directory

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
