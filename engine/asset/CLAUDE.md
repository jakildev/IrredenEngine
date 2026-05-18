# engine/asset/ — Binary + text asset formats: `.vxs`, `.rig`, `.irsprite`

This module is the engine's general asset-format home — binary and text
asset formats land here. Current formats:

- **Sprite-sheet sidecars** — plain-text metadata for PNG atlases with
  extension `.irsprite`. The PNG itself is loaded by `engine/render/`'s
  `ImageData`; this module owns only the sidecar.
- **Voxel sets** — `.vxs` binary + `.vxs.json` sidecar (dense per-voxel
  records and/or SDF shape-group composition; see editor-epic design doc).
- **Rigs** — `.rig` binary + `.rig.json` sidecar (joint hierarchy + bind
  points + skeletal animation tracks).

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

- `saveSpriteSheetMeta(name, path, meta)` /
  `loadSpriteSheetMeta(name, path)` — sidecar text round-trip.
- `saveShapeGroup(path, span<ShapeRecord>)` / `loadShapeGroup(path)` —
  `.vxs` SHAPES-mode SDF primitive composition round-trip. See
  `voxel_set_format.hpp` for record layout and the
  "How to add a new SDF primitive" walkthrough below.
- `saveRig(name, path, rig)` / `loadRig(name, path)` — joints-first
  rig asset round-trip (see `.rig format` below). Buffer-mode
  `writeRig` / `readRig` are exposed for in-memory consumers.

The `name` is embedded in the trixel file header; the `path` is the
output directory. For sprite sheets, `name` is the basename shared
by the `.png` atlas and its `.irsprite` sidecar. For rigs, `name` is
the basename shared by the `.rig` binary and its `.rig.json` sidecar.

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

## .rig format

`.rig` v1 — joint hierarchy + bind points. Chunks are added without
bumping the file version; older readers silently skip unknown tags
(Save Format Extensibility Rule #1). Animation keyframes (#606) will
land the same way when that task ships.

```
AssetHeader { magic = "IRRG", version = 1, chunkCount }
ChunkTableEntry[chunkCount]
JNTS chunk body:
    varuint  jointCount
    repeat jointCount times:
        uint16   recordVersion    (Rule #3 per-record additive-only)
        float32  rotation { x, y, z, w }    // quaternion as { qw, qx, qy, qz }
        float32  translation { x, y, z, w }
        uint32   parentIndex
        string   name             // varuint-prefixed UTF-8, may be empty
BIND chunk body (optional — omitted when no bind points):
    varuint  bindPointCount
    repeat bindPointCount times:
        uint16   recordVersion    (Rule #3 per-record additive-only)
        uint32   boneId
        float32  offset { x, y, z }
        float32  rotation { x, y, z, w }
        string   name             // varuint-prefixed UTF-8, may be empty
```

The accompanying `<name>.rig.json` sidecar emits per-joint
`{ index, name, parentIndex }` and per-bind-point `{ index, name, boneId }`
for designer diffing. Sidecars are write-only; the loader ignores them
(Rule #6).

The asset-side `IRAsset::Rig` is distinct from the runtime
`IRComponents::C_JointHierarchy` (no names at draw time) — translate
via `engine/prefabs/irreden/voxel/rig_bridge.hpp`
(`IRPrefab::Rig::toComponent` / `fromComponent`). Splitting the two
keeps `engine/asset/` independent of the prefab voxel component.

## Typical usage

Creations call this during tool runs (e.g. the `shape_debug` demo dumps
generated trixel shapes to disk for later loading) and during
import/export. Runtime gameplay code generally does not touch this module.

## Binary-I/O primitives (shared across all new formats)

The headers under `engine/asset/include/irreden/asset/` provide the
read/write building blocks every new asset format uses. Existing
formats (`.irsprite`) predate the contract; new formats must
use these.

**Hard rule — no raw `fopen` + `fwrite` / `fread` for new format I/O.**
All new binary format save/load paths route through
`FileBinaryWriter` / `FileBinaryReader` (or `MemoryBinaryWriter` /
`MemoryBinaryReader` for in-memory round-trips); all reads return
`Result<T>` and surface errors with file path + byte offset. Raw
`fwrite` / `fread` is forbidden outside the two legacy formats listed
above — those will be migrated or deleted, not extended.

The historical liability is concrete: `.txl`'s raw-binary save and
load paths in `engine/asset/src/ir_asset.cpp` left every `fwrite` and
`fread` return value unchecked. A truncated file loaded as garbage
with no error; a corrupted save succeeded silently. The
`BinaryWriter` / `BinaryReader` abstraction exists exactly to make
that class of bug unrepresentable for new code.

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
versioning (`kShapeRecordVersion`, `kVoxelRecordVersion`) covers
field-level evolution inside records (Extensibility Rule #3).

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
- `BNDS` — DENSE-mode bounds. Six i32 (`min.xyz, max.xyz`); inclusive
  min, exclusive max. `voxelCount() = (max - min).x * .y * .z`.
- `VOXR` — DENSE-mode per-voxel records. `kVoxelRecordVersion` u16 +
  varuint count + tightly-packed 12 B records matching `C_Voxel`. A
  count mismatch against the bounds-derived voxel volume logs a warning
  and proceeds with the chunk's count (Rule #5).
- `LAYR` — DENSE-mode named layer membership bitmasks. One layer is
  `(name, ceil(voxelCount/64) u64 words)`; bit i = membership of voxel
  i.
- `FRAM` — DENSE-mode per-frame position offsets. Each frame is
  `(frameIndex u32, varuint offsetCount, offsetCount × vec3)`. A frame
  whose `offsetCount != voxelCount` is dropped and counted in
  `skippedFrames_` so a corrupt or partial frame can't corrupt the
  whole asset.
- `META` — DENSE-mode free-form `(key, value)` UTF-8 string pairs.

SHPG record layout (one per primitive):

```
uint32  shapeTypeId      // numeric; resolve via SREF name lookup
uint16  recordVersion    // additive-only field evolution (Rule #3)
float32 params[4]        // SDF parameters (semantics per ShapeType)
uint32  packedRGBA       // Color::toPackedRGBA()
uint32  flags            // IRMath::SDF::ShapeFlags bit field
uint8   boneId           // joint binding (T-146 / T-169); 0 = none
float32 offset[3]        // local translation
float32 rotation[4]      // quaternion (x, y, z, w)
uint8   csgOp            // CsgOp: NONE | UNION | SMOOTH_UNION | SUBTRACT | INTERSECT
```

High-level entry points:

- `saveShapeGroup(path, span<ShapeRecord>)` — writes MODE=SHAPES, SREF,
  SHPG chunks under the `VXS1` header. Emits a `.vxs.json` sidecar at
  `path + ".json"` (Rule #6) on successful binary write.
- `loadShapeGroup(path)` — returns `VoxelSetFile { mode_, shapeRecords_,
  unknownShapesSkipped_ }` after resolving SREF and SHPG. Container
  errors (`BadMagic`, `VersionTooNew`, truncation, chunk-out-of-bounds)
  surface as `BinaryIOError` results.
- `saveDenseVoxelSet(path, DenseVoxelSet)` — writes MODE=DENSE plus
  BNDS, VOXR, and (if non-empty) LAYR / FRAM / META chunks. Emits a
  `.vxs.json` sidecar at `path + ".json"` (Rule #6) on successful
  binary write.
- `loadDenseVoxelSet(path)` — returns `DenseVoxelSetFile { mode_,
  dense_, skippedFrames_ }`. Loading a SHAPES-only file through this
  path is a no-op: `mode_ = SHAPES` and `dense_` stays empty.
- `saveVoxelSet(path, span<ShapeRecord>, DenseVoxelSet)` — writes
  MODE=HYBRID with SREF + SHPG (shapes) and BNDS + VOXR (dense). Emits
  a `.vxs.json` sidecar at `path + ".json"` (Rule #6) on successful
  binary write.
- `loadVoxelSet(path)` — unified loader; returns `VoxelSetAllFile {
  mode_, shapeRecords_, dense_, ... }`. Works for all three modes —
  SHAPES-only leaves `dense_` empty; DENSE-only leaves `shapeRecords_`
  empty; HYBRID populates both.

For callers working with `C_ShapeDescriptor`, the prefab-side adapter at
`engine/prefabs/irreden/asset/voxel_set_io.hpp` (`#include
<irreden/asset/voxel_set_io.hpp>`) exposes
`IRAsset::saveVoxelSet(path, span<const C_ShapeDescriptor>, ...)` with parallel
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

The `simplify` skill runs the version-bump detection policy below whenever a
`.hpp` or `.cpp` file under `engine/asset/`, `engine/prefabs/irreden/voxel/`,
or `engine/world/` is in the diff; `review-pr` applies the same principles
via its step 4 Serialization checklist.

### Automated version-bump detection

**Check 1 — annotated structs:**

1. Scan the diff's `+` lines for struct fields (member variable declarations)
   inside a struct body whose `struct <Name>` declaration is preceded **on the
   immediately prior line** by `// IRAsset: serialized`. The annotation must be
   on the line directly above `struct <Name>` — an annotation elsewhere in the
   file does not apply to subsequent structs.

2. For each struct type whose field layout changed (added/removed/renamed field
   on a `+` or `-` line), check whether the diff also contains a corresponding
   `kSaveVersion` change on a `+` line in the same file (or a sibling sidecar
   file for the format):

   ```
   static constexpr uint16_t kSaveVersion = N;
   ```

3. If the field layout changed but no `kSaveVersion` bump appears in the diff,
   emit a finding — **do not auto-fix**, this needs human judgment:

   ```
   reported 1 finding for review:
     - <path>:<line> — <StructName> is annotated // IRAsset: serialized and
       its field layout changed, but kSaveVersion was not bumped.
       Add `static constexpr uint16_t kSaveVersion = N+1;` and a migration
       entry in the format's reader for saves written at the old version.
   ```

**False-positive guard — do NOT flag:**
- Changes to method bodies, constructors, or `static` helper functions within
  the struct (these do not affect binary layout).
- A struct whose `// IRAsset: serialized` annotation was itself added in the
  same diff — version 1 never needs a migration.
- Changes to the `kSaveVersion` line itself.
- Changes that only touch comments or whitespace inside the struct.

**Check 2 — unannotated serialized structs:**

The check above protects structs that are *already* annotated. A second class
of bug is structs whose fields are individually serialized but which lack the
`// IRAsset: serialized` annotation entirely — the version-bump check then
never fires, even on layout changes that need a migration. `ShapeRecord`
(`engine/asset/include/irreden/asset/voxel_set_format.hpp`) shipped in
T-168 / T-170 without the annotation despite each of its fields being
individually written and read by the format's chunk handlers; the gap stayed
invisible until an audit pass surfaced it.

1. Scan the diff's `+` lines for any sequence of two or more
   `<writer>.write*(<expr>.<field>_)` calls where `<expr>` is a reference to a
   struct instance and `<field>_` is a public member. Likewise for
   `<reader>.read*` paired with assignment back to `<expr>.<field>_`.
   Two-or-more is the heuristic for "this struct is being serialized
   field-by-field" rather than "one field of a struct happens to be written."

2. For each struct type touched this way, resolve its declaration (same file or
   sibling header) and check whether the declaration is preceded by
   `// IRAsset: serialized` on the immediately prior line per the annotation
   rule.

3. If the struct is serialized field-by-field and lacks the annotation, emit a
   finding — **do not auto-fix**, the annotation carries a `kSaveVersion`
   constant whose initial value is a human call:

   ```
   reported 1 finding for review:
     - <header-path>:<line> — <StructName> is serialized field-by-field
       by <function-path>:<line> but lacks the `// IRAsset: serialized`
       annotation. Add the comment line directly above the struct
       declaration plus a `static constexpr uint16_t kSaveVersion = 1;`
       member so the version-bump check covers future changes.
   ```

**False-positive guard for Check 2 — do NOT flag:**
- Structs that only carry transient binary I/O state (`BinaryStatus`,
  `LoadedChunk`, `ChunkPayload`, `Result<T>`). These pass through format code
  without being the format's persisted record.
- Structs declared in `binary_io.hpp` or `chunk_header.hpp` (under
  `engine/asset/include/irreden/asset/`) — framework types, not format records.
- Cases where the writer/reader calls operate on a temporary or a
  function-local variable that doesn't correspond to a named struct type.

Not every asset-related struct needs this annotation — only those whose fields
are **directly serialized** into a chunk body byte-for-byte. Framework structs
(`AssetHeader`, `ChunkTableEntry`, `LoadedChunk`) are governed by the file-level
version in `AssetHeader.version_`, not per-struct versioning.

## Gotchas

- **Path joining.** Paths are composed with `IRUtility::joinPath`.
  Windows vs POSIX separators are handled there, but double-check when
  you're debugging a missing-file bug.
- **Not a general asset pipeline.** Don't add shader hot-reload, audio
  decoding, or model loading here without a design pass — the file name
  suggests an abstraction that doesn't exist yet.
- **Voxel image stub.** The `FileTypes` enum in `ir_asset.hpp` has a
  `kVoxelImage` value with no corresponding load/save routine — still an
  aspirational stub. `kSpriteImage` is paired with the `.irsprite` sidecar
  routines above.
