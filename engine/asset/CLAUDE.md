# engine/asset/ — trixel texture I/O

Tiny module. The only thing it does today is save and load trixel textures
(a `(size, colors[], distances[])` tuple) to disk as a raw binary format.

`IRAsset::` exposes two free functions:

- `saveTrixelTextureData(name, path, size, colors, distances)` —
  writes a binary file.
- `loadTrixelTextureData(name, path, size, colors, distances)` —
  reads one.

The `name` is embedded in the file header; the `path` is the output
directory.

## Format

Raw binary, no versioning:

```
ivec2 size
<size.x * size.y> Color        entries
<size.x * size.y> Distance     entries
```

No checksum, no magic bytes, no compression. Treat the file as volatile.

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
- **Only trixel data.** The `FileTypes` enum in `ir_asset.hpp` has
  `kSpriteImage` and `kVoxelImage` values but no corresponding load/save
  routines — they're aspirational stubs only.
