# engine/asset/ — trixel texture + sprite-sheet I/O

Tiny module. Today it covers two file formats:

- **Trixel textures** — raw binary `(size, colors[], distances[])` blobs
  with extension `.txl`.
- **Sprite-sheet sidecars** — plain-text metadata for PNG atlases with
  extension `.irsprite`. The PNG itself is loaded by `engine/render/`'s
  `ImageData`; this module owns only the sidecar.

## What this module is NOT

`.txl` is the **trixel-texture** format and stays that way. Voxel-set
authoring (per-voxel records, SDF shape groups) is **not** owned by
this module — it lives in the new `engine/persist/` module under its
own extension `.vxs` (see the editor-epic design doc's
"Per-voxel record extension" and "Save format extensibility rules"
sections). Do not extend `.txl` to carry voxel data; the two existing
trixel-texture call sites
(`engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp`)
would have to thread a multi-format reader through unrelated trixel-canvas
code. If you find yourself reaching for that, file an issue instead.

`IRAsset::` exposes:

- `saveTrixelTextureData(name, path, size, colors, distances)` /
  `loadTrixelTextureData(...)` — trixel binary round-trip.
- `saveSpriteSheetMeta(name, path, meta)` /
  `loadSpriteSheetMeta(name, path)` — sidecar text round-trip.

The `name` is embedded in the trixel file header; the `path` is the
output directory. For sprite sheets, `name` is the basename shared
by the `.png` atlas and its `.irsprite` sidecar.

## Trixel format

Raw binary, no versioning:

```
ivec2 size
<size.x * size.y> Color        entries
<size.x * size.y> Distance     entries
```

No checksum, no magic bytes, no compression. Treat the file as volatile.

## Sprite-sheet sidecar format

Plain text, line-oriented, order-independent. See `ir_asset.hpp` for the
full `SpriteSheetMeta` / `SpriteAnimationDesc` field set. Atlas pixel
dimensions are NOT stored — they are read from the PNG at load time.

```
# comment line (ignored)
cellWidth  <pixels>
cellHeight <pixels>
margin     <pixels>   (optional, default 0)
padding    <pixels>   (optional, default 0)
anim <name> <firstFrame> <frameCount> <fps>
```

Animation names must not contain whitespace (the loader scans with
`%127s` and would silently truncate); `saveSpriteSheetMeta` asserts on
this at write time.

## Typical usage

Creations call this during tool runs (e.g. the `shape_debug` demo dumps
generated trixel shapes to disk for later loading) and during
import/export. Runtime gameplay code generally does not touch this module.

## Gotchas

- **`fwrite`/`fread` return values unchecked.** A truncated file loads as
  garbage with no error. Check return values against the expected element
  count if correctness matters. `fopen` failures are caught (returns early
  with `IRE_LOG_ERROR`).
- **Path joining.** Paths are composed with `IRUtility::joinPath`.
  Windows vs POSIX separators are handled there, but double-check when
  you're debugging a missing-file bug.
- **Not a general asset pipeline.** Don't add shader hot-reload, audio
  decoding, or model loading here without a design pass — the file name
  suggests an abstraction that doesn't exist yet.
- **Voxel image stub.** The `FileTypes` enum in `ir_asset.hpp` has a
  `kVoxelImage` value with no corresponding load/save routine — still an
  aspirational stub. (`kSpriteImage` is paired with the `.irsprite`
  sidecar routines above.)
