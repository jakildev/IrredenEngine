# engine/asset/ — trixel texture + sprite-sheet I/O

Tiny module. Today it covers two file formats:

- **Trixel textures** — raw binary `(size, colors[], distances[])` blobs
  with extension `.txl`.
- **Sprite-sheet sidecars** — plain-text metadata for PNG atlases with
  extension `.irsprite`. The PNG itself is loaded by `engine/render/`'s
  `ImageData`; this module owns only the sidecar.

## What this module is and isn't

`.txl` is the **trixel-texture** format and stays that way. Do not
extend `.txl` to carry voxel data; the two existing trixel-texture
call sites (`engine/prefabs/irreden/render/components/component_triangle_canvas_textures.hpp`)
would have to thread a multi-format reader through unrelated
trixel-canvas code. If you find yourself reaching for that, file an
issue instead.

This module is the engine's general asset-format home — meaning the
new voxel-set, rig, and prefab formats land here alongside the
existing trixel-texture and sprite-sheet I/O:

- `.vxs` — voxel-set asset (dense per-voxel records and/or SDF
  shape-group composition; see editor-epic design doc).
- `.rig` — joint hierarchy + bind points + skeletal animation tracks.
- `.prefab.lua` — Lua prefab template referencing `.vxs` + `.rig` +
  component pack.

The binary-I/O primitives (`BinaryWriter`/`Reader`, chunk-table header
helpers, name-table encoding, JSON sidecar emitter) used by every new
format also live here so consumers have one place to look. All formats
obey the seven [Save format extensibility rules](#save-format-extensibility-rules)
below — the same list lives in `docs/design/entity-editor-epic.md` as
the design-doc home of the rules; this module's contract is to provide
the primitives that make those rules cheap to follow.

The one save/load surface that does **not** live here is the ECS world
snapshot (issue #199). It walks the archetype graph, so it lives in
`engine/world/` and consumes this module's binary primitives —
`engine/asset/` stays at its current dependency level (below `world/`,
`entity/`) and does not depend on either.

`IRAsset::` exposes:

- `saveTrixelTextureData(name, path, size, colors, distances)` /
  `loadTrixelTextureData(...)` — trixel binary round-trip.
- `saveSpriteSheetMeta(name, path, meta)` /
  `loadSpriteSheetMeta(name, path)` — sidecar text round-trip.
- `saveShapeGroup(path, span<ShapeRecord>)` / `loadShapeGroup(path)` —
  `.vxs` SHAPES-mode SDF primitive composition round-trip. See
  `voxel_set_format.hpp` for record layout and the
  "How to add a new SDF primitive" walkthrough below.

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

## Binary-I/O primitives (shared across all new formats)

The headers under `engine/asset/include/irreden/asset/` provide the
read/write building blocks every new asset format uses. Existing
formats (`.txl`, `.irsprite`) predate the contract; new formats must
use these.

### `binary_io.hpp` — `BinaryWriter` / `BinaryReader`

Abstract sinks/sources with two backends each:

- `FileBinaryWriter` / `FileBinaryReader` — wraps a `FILE*` in `wb`/`rb`
  mode.
- `MemoryBinaryWriter` / `MemoryBinaryReader` — backed by a
  `std::vector<uint8_t>` (writer) or a borrowed byte span (reader).

All primitives are little-endian, fixed-width:

- `writeU8/U16/U32/U64`, signed `writeI*`, and matching `read*` returning
  `Result<T>`.
- `writeF32/F64` / `readF32/F64` — IEEE 754 bit pattern, host-byte-order
  agnostic.
- `writeVarUInt(u64)` / `readVarUInt()` — ULEB128 (Protocol Buffers
  style). 1 byte for values < 128, up to 10 bytes for `u64`.
- `writeString(string_view)` / `readString()` — UTF-8 with a varuint
  byte-length prefix.

Reads return `Result<T>` with a `BinaryStatus status_` and the typed
`value_`. Check `r.ok()` before consuming `r.value_`. Truncated files,
malformed varints, and length-prefixed strings that exceed the
remaining buffer all surface as recoverable errors with file path +
byte offset in the diagnostic.

### `chunk_header.hpp` — file header + chunk table

Every binary asset format starts with the 12-byte header

```
char magic[4];
uint32 version;
uint32 chunkCount;
```

followed by `chunkCount` entries of `{ char tag[4]; uint64 offset;
uint64 size; }` (20 bytes each), then the chunk bodies. Use
`writeChunked(writer, magic, version, span<ChunkPayload>)` on save —
it computes offsets and writes header + table + bodies in one pass.
On load, `readChunks(reader, expectedMagic, maxKnownVersion)` returns
a `std::vector<LoadedChunk>` (every chunk's bytes copied into memory)
plus the header. Unknown chunks come through with their raw bytes
intact so loaders silently skip them (**Extensibility Rule #1** — no
version bump needed when a future writer adds a new chunk).

### `name_table.hpp` — enum name-table chunk

`writeNameTable(w, span<NameTableEntry>)` and `readNameTable(r)`
serialize `(uint32 id, string name)` pairs into a chunk body.
`NameTable` is the in-memory lookup helper (`idByName(name)` and
`nameById(id)`) consumers use to translate disk-side ids to the
current build's enum values. **Extensibility Rule #2**: prefer
`name → current enum` lookup, fall back to id only when the name
table is absent, so a save written by a future build with a new
`ShapeType` value loads on an older build as "unknown shape, skipped"
— not "corrupt file."

### `voxel_set_format.hpp` — `.vxs` voxel-set asset

The voxel-set container holds three persistence modes — DENSE (per-voxel
records, T-167), SHAPES (SDF composition, T-168), HYBRID (DENSE + SHAPES,
T-668) — under one magic (`VXS1`) and one version (`kVoxelSetVersion = 1`).
Magic + version together govern the **container**; per-record additive
versioning (`kShapeRecordVersion`) covers field-level evolution inside
records (Extensibility Rule #3).

Container chunks:

- `MODE` — 4-byte ASCII mode tag (`DENS` / `SHPS` / `HYBR`). Missing →
  defaults to SHAPES (legacy single-mode files). Unknown tag → returns
  `VoxelSetMode::UNKNOWN` with a logged warning; the caller decides
  whether to refuse the asset or render what's parseable.
- `SREF` — `ShapeType` id↔name table; `buildCurrentShapeTypeNameTable()`
  emits the writer's view of the canonical enum so a future build's
  save survives the read on an older build (name lookup misses → record
  skipped, no failure).
- `SHPG` — SHAPES-mode primitive records (`ShapeRecord` array). Records
  with an unresolvable disk-side `ShapeType` are skipped and counted
  in `unknownShapesSkipped_`.

SHPG record layout (one per primitive):

```
uint32  shapeTypeId      // numeric; resolve via SREF name lookup
uint16  recordVersion    // additive-only field evolution (Rule #3)
float32 params[4]        // SDF parameters (semantics per ShapeType)
uint32  packedRGBA       // Color::toPackedRGBA()
uint32  flags            // IRRender::ShapeFlags bit field
uint8   boneId           // joint binding (T-146 / T-169); 0 = none
float32 offset[3]        // local translation
float32 rotation[4]      // quaternion (x, y, z, w)
uint8   csgOp            // CsgOp: NONE | UNION | SMOOTH_UNION | SUBTRACT | INTERSECT
```

High-level entry points:

- `saveShapeGroup(path, span<ShapeRecord>)` — writes MODE=SHAPES, SREF,
  SHPG chunks under the `VXS1` header.
- `loadShapeGroup(path)` — returns `VoxelSetFile { mode_, shapeRecords_,
  unknownShapesSkipped_ }` after resolving SREF and SHPG. Container
  errors (`BadMagic`, `VersionTooNew`, truncation, chunk-out-of-bounds)
  surface as `BinaryIOError` results.

For callers working with `C_ShapeDescriptor`, the prefab-side adapter at
`engine/prefabs/irreden/asset/voxel_set_io.hpp` (`#include
<irreden/asset/voxel_set_io.hpp>`) exposes
`IRAsset::saveVoxelSet(span<const C_ShapeDescriptor>, ...)` with parallel
offset / rotation / csgOp / boneId arrays — the per-entity composition
metadata `C_ShapeDescriptor` does not itself carry. The adapter lives in
prefabs to keep `engine/asset/` from depending on the component layer.

#### How to add a new SDF primitive

Adding a new shape that participates in `.vxs` shape-group saves is
designed to require **no format change**:

1. Append the new enum value to `IRMath::SDF::ShapeType` in
   `engine/math/include/irreden/math/sdf.hpp` (e.g. `TORUS_KNOT = 10`).
2. Append the corresponding `(id, name)` pair to the `kShapeTypeTable`
   constant in `engine/asset/src/voxel_set_format.cpp`. The name string
   is the disk-side identity; older builds use it to recognize the new
   type via SREF name-lookup, drop a "unknown shape, skipped" warning,
   and load the rest of the asset cleanly.
3. Add the SDF evaluator: a new `inline float yourShape(vec3 p, ...)`
   in `sdf.hpp`, a new `IRMath::SDF::ShapeType` dispatch case in
   `IRMath::SDF::evaluate` / `boundingHalf`, and matching GLSL +
   Metal implementations under `engine/render/src/shaders/`. The
   trixel pipeline (`SHAPES_TO_TRIXEL`) picks it up automatically once
   the dispatch tables route to your evaluator.

Steps 1 + 2 + the shader pair are sufficient for an old `.vxs` save to
keep loading after a future build adds `TORUS_KNOT`. The format version
stays at `1`; the per-record `recordVersion_` field is the lever for
future field additions inside the record itself (Rule #3 — append +
bump + default the new field in the older-build load path).

### `json_sidecar.hpp` — write-only JSON emitter

`JsonSidecarWriter` builds a pretty-printed JSON document via
`beginObject` / `endObject` / `beginArray` / `endArray` /
`key(string_view)` / `value*` calls. No third-party dep. **Read side
is intentionally not implemented** — sidecars are regenerated from the
binary on every save (**Extensibility Rule #6**). Extending the binary
side never forces a sidecar schema migration; the emitter just learns
the new field.

## Save format extensibility rules

Every binary asset format the engine ships (`.vxs`, `.rig`, the ECS
world snapshot in `engine/world/`, any future format) obeys the seven
rules below. They're the constitution for the binary-I/O primitives
in this module and every consumer of them; the long-form rationale and
worked examples live in `docs/design/entity-editor-epic.md`. The summary:

1. **Chunk-table forward compatibility.** Loaders silently skip
   unknown chunk tags. Writers append new chunks without bumping the
   file version. Order in the chunk table is not load-bearing.
2. **Enums travel by name + id.** Anywhere a registered enum is
   stored (`ShapeType`, `ComponentId`, `RelationType`, `MaterialId`,
   bind-point semantic role), write both the numeric id and a
   string-name table. On load, prefer name → current enum lookup; fall
   back to id only when the name table is absent.
3. **Per-component / per-record additive-only versioning.** Each
   component (in the world snapshot) and each record type (per-voxel
   record, joint, bind-point, shape primitive) carries a `uint16`
   version alongside its blob. Field additions append + bump the
   version; the load migration defaults the appended fields. Removals
   and renames require an explicit migration function keyed by
   `(typeId, oldVersion)`. **No silent structural changes.**
4. **Relations as first-class data, not hard-coded.** The relation
   chunk stores `(relationTypeId, entityA, entityB)` triples with the
   name table at the chunk head — adding `OWNS`, `ATTACHED_TO`,
   `EQUIPPED_BY` is a one-line enum extension.
5. **Unknown is recoverable, never fatal.** Bad magic, truncated
   file, version newer than the loader knows about → clear diagnostic
   with file path + offset, return an empty/default value, never
   crash. Test fixtures: corrupt-magic, truncated-mid-chunk,
   version-too-new, unknown-chunk-tag, unknown-enum-value.
6. **JSON sidecar is regenerated from the binary, never the source
   of truth.** Emit on save, ignore on load.
7. **Save/load surface is documented in one place per format.** Each
   asset format gets a header block in its `.hpp` describing the
   chunk table, current chunks, version history, and the migration
   registry.

## Serialized-struct annotation

Any C++ struct whose fields are **individually serialized into the asset
format** — per-voxel records, joint records, bind-point descriptors,
world-snapshot component blobs — must carry two markers so automated checks
can enforce Rule #3:

1. A `// IRAsset: serialized` line comment on the line immediately
   preceding the `struct` declaration.
2. A `static constexpr uint16_t kSaveVersion = N;` inside the struct body.

Example:

```cpp
// IRAsset: serialized
struct JointRecord {
    static constexpr uint16_t kSaveVersion = 1;

    IRMath::vec4 rotation_{0.f, 0.f, 0.f, 1.f};
    IRMath::vec4 translation_{0.f, 0.f, 0.f, 0.f};
    std::uint32_t parentIndex_ = 0;
    std::string name_;  // serialized via writeString(), not raw fwrite
};
```

Fixed-size POD fields (numeric scalars, `IRMath::vec*`) are written by
direct `fwrite` of their bytes; variable-length fields like `std::string`
go through `writeString()` / `readString()`. The struct itself is not
`fwrite`'d as a single blob — the per-field serialization is what
`kSaveVersion` covers.

**When you add, remove, or rename a field:**

1. Increment `kSaveVersion`.
2. Add a reader migration in the format's load function keyed on
   `(structType, oldVersion)`.
3. Update the per-format save/load header block (Extensibility Rule #7).

The `simplify` skill (pre-commit) and `review-pr` skill both scan for
`// IRAsset: serialized` structs with changed field layouts and emit a finding
when `kSaveVersion` was not bumped. See `.claude/skills/simplify/SKILL.md`
section 2c and `.claude/skills/review-pr/SKILL.md` step 4 "Serialization".

Not every asset-related struct needs this annotation — only those whose fields
are **directly serialized** into a chunk body byte-for-byte. Framework structs
(`AssetHeader`, `ChunkTableEntry`, `LoadedChunk`) are governed by the file-level
version in `AssetHeader.version_`, not per-struct versioning.

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
