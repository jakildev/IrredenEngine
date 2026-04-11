# engine/asset/ — trixel texture I/O

Tiny module. The only thing it does today is save and load trixel textures
(a `(size, colors[], distances[])` tuple) to disk as a raw binary format.

## Entry point

`engine/asset/ir_asset.hpp` — exposes two free functions in `IRAsset::`:

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

## Internal layout

```
engine/asset/
├── ir_asset.hpp        — public facade
└── src/
    └── ir_asset.cpp    — file I/O
```

## Gotchas

- **Extension mismatch.** `save` writes `.txl`, `load` reads `.irtxl` in
  the current code. Either name the file consistently or pre-rename
  before loading. Verify the actual extensions in `src/ir_asset.cpp`
  before shipping anything that depends on them.
- **No error handling.** `fwrite`/`fread` failures aren't checked. A
  truncated file loads as garbage. Wrap calls in `std::filesystem::exists`
  and size checks.
- **Path joining.** Paths are composed with `IRUtility::joinPath`.
  Windows vs POSIX separators are handled there, but double-check when
  you're debugging a missing-file bug.
- **Not a general asset pipeline.** Don't add shader hot-reload, audio
  decoding, or model loading here without a design pass — the file name
  suggests an abstraction that doesn't exist yet.
- **Only trixel data.** The commented-out `kSpriteImage`/`kVoxelImage`
  enum values are aspirational; there are no load/save routines for
  them.
