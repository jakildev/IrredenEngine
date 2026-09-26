# Fog-of-war world field

- **Status:** normative design contract. The implementation lands in phases;
  the phase map below separates the contract from current code.
- **Owning subsystems:** `engine/prefabs/irreden/render/` (field, window and
  source tier), `engine/prefabs/irreden/spatial/` (chunked storage), and
  `engine/world/` (region persistence).
- **Tracking:** #3663.

---

## Invariant

Every integer world column has an unexplored, explored or visible state,
wherever the camera is. Persistent state survives the camera leaving and
returning and round-trips through an opted-in save root. The GPU sees a
camera-anchored window over that unbounded CPU field; the window is a cache,
never the authority.

The contract is exact for matter in the camera depth slab. Beyond that slab,
or beyond a capped window, the conservative result is unexplored: geometry can
be over-fogged but must not leak through fog.

## D10 — Coverage contract

The camera depth slab is `z in [-128, 128)` around the z = 0 plane, matching
the light-occlusion grid's exact slab. The fog window covers every column whose
matter in that slab can land anywhere on the fog canvas at any zoom at least
1, visual yaw or pivot.

All four shader tap pairs return grid state unexplored for a column outside
the window. Analytic vision circles still max-compose with that result. The
1x1 placeholder bound for a canvas without fog remains visible.

The deliberate limits are:

- matter outside the depth slab can be over-fogged;
- the periphery beyond a capped window can be over-fogged; and
- off-screen shadow casters outside the window can be culled as unexplored
  matter already is.

Reading out-of-window columns as visible is rejected because tall or deep
matter could then leak through fog. Content-aware resizing is rejected because
it requires a per-frame voxel-bound reduction and texture reallocation. A
creation-tunable slab remains an additive future extension.

## D1 — Chunked storage

The authority is `IRPrefab::Spatial::ChunkedField2D<std::uint8_t>`: sparse,
dense 32x32 field chunks keyed by `FieldChunkKey`. One cell represents one
integer world column. An absent chunk reads as unexplored.

The generic storage surface has four mutation operations:

| Operation | Contract |
|---|---|
| `setCell(cell, value) -> bool` | Inserts as needed and returns true only when presence or value changed. |
| `eraseChunk(chunkCoord) -> bool` | Explicitly removes a present chunk, retains its buffer for reuse and records the key dirty. |
| `assignChunk(chunkCoord, cells) -> bool` | Inserts or replaces exactly 1024 cells, recounts non-zero cells and reports whether anything changed. |
| `fillRow(firstCell, count, value) -> int` | Writes along +x across chunk boundaries, resolves each touched field chunk once and returns the number of changed cells. |

Every mutation path keeps `nonZeroCount_` exact and marks a field chunk dirty
if and only if its presence or contents changed. `min_` and `max_` retain their
existing contract: `update()` refreshes them. Presence remains map membership;
only `clear()` and `eraseChunk()` remove a field chunk.

A raw mutable span plus a later dirty mark is rejected. It cannot preserve the
non-zero count without a recount, and a forgotten mark silently leaves derived
data stale. `fillRow` serves run writes and `assignChunk` serves whole-chunk
loads without exposing that failure mode.

The field is not keyed by 3D residency chunks: those choose an arbitrary z
slice, belong to an optional streaming manager, and expose no suitable
observer surface. A fog-private map is also rejected because it would repeat
the field kit's signed floor mapping and key packing.

## D2 — Ownership

`IRPrefab::Fog::WorldField` is GPU-free and is held by
`C_CanvasFogOfWar` through a `std::shared_ptr` created in the component
constructor. ECS copies alias the field just as they alias the texture;
`onDestroy()` releases both. No destructor performs disk I/O.

`WorldField` owns the persistent layer, the transient source-tier layer, region
residency bookkeeping and an optional persistence handle. Tests construct it
without a render device. The component owns the GPU window texture, observer
data and the origin that describes the texture's current contents.

The legacy `cpuBuffer_`, `dirty_` and `allUnexplored_` leave the component.
Change tracking becomes the pending set in D5, so the fog exception to the
no-dirty-flags rule leaves with them. A by-value field is rejected because the
ECS copies components while the field kit owns move-only buffers; a deep copy
would also duplicate every resident chunk on an archetype move. A singleton is
rejected because it separates field lifetime from its fog canvas.

## D3 — Unbounded CPU API

`setCell`, `getCell`, `revealRadius` and `clear` accept any representable
integer world column and do not test the GPU window. Public prefab, component
and Lua service names remain unchanged.

`revealRadius` clamps its radius to `kFogRevealRadiusMax = 1024` and computes
row bounds in 64-bit arithmetic, so a centre near an int32 limit touches only
representable cells. The Lua binding keeps its existing preflight rejection
when the requested disc would cross the int32 bounds; the C++ field operation
performs the clamped write. `kFogOfWarSize` and `kFogOfWarHalfExtent` cease to
bound the fog grid after the window phase. The LOS layout gets its own
`kFogLosFieldSize = 256` and `kFogLosFieldHalfExtent = 128` constants.

Read-only diagnostics expose `windowEdge()`, `windowOrigin()` and
`fieldStats()`. The statistics contain resident region and field-chunk counts,
plus region probes, loads, saves and evictions since the previous read.

## D4 — Region persistence

Persistence is opt-in and stores one file per 16x16 field chunks. A region is
512x512 world columns and is identified by
`(rx, ry) = (IRMath::floorDiv(cx, 16), IRMath::floorDiv(cy, 16))`, where
`(cx, cy)` is a field chunk coordinate.

`IRWorld::FieldChunkDiskPersistence::create(saveRoot, layer, bytesPerCell)` is
the only constructor. It rejects an empty root and accepts a layer only when it
matches `[a-z][a-z0-9_]{0,31}`. The layer is one safe path segment; fog uses
`fog` with one byte per cell.

The path is:

```text
<saveRoot>/fields/<layer>/<IRMath::floorDiv(rx,64)>/<IRMath::floorDiv(ry,64)>/
    <sx><10-digit-abs-rx>_<sy><10-digit-abs-ry>.irfield
```

Coordinates in the filename are signed and zero-padded. Bucket coordinates
are region coordinates, not field-chunk coordinates.

### File format

The container uses the engine's chunked asset format.

| Field | Bytes / layout | Contract |
|---|---|---|
| Magic | `IRFD` | Identifies a field-region asset. |
| Version | 1 | Unknown future versions are rejected by the normal asset-version contract. |
| `FHDR` | `int32 rx`, `int32 ry`, `uint8 chunkEdge`, `uint8 regionEdge`, `uint8 bytesPerCell` | Edges are 32 and 16. The requested region key and configured schema must match. The layer is represented by the validated path, not by header bytes. |
| `CMSK` | 32 bytes | Region-local chunk `(lx, ly)` is bit `i = ly * 16 + lx`, stored in byte `i / 8`, bit `i % 8`, LSB first. |
| `CELL` | `popcount(CMSK) * 1024 * bytesPerCell` | Present chunks appear in ascending mask-bit order; each chunk is row-major `y * 32 + x`. |

Unknown chunks are skipped. A missing file is a quiet absence. Bad magic,
truncation, a mismatched region key or configured schema, and a `CELL` size
that disagrees with the mask are malformed and return absence with a warning.

`loadRegion` is the single probe: one quiet `fopen`, one in-memory read and
parsing through `MemoryBinaryReader`. It does not preflight with `exists()` and
does not use `FileBinaryReader`, whose open failure is noisy.

`saveRegion` writes a sibling temporary file and renames it over the target.
Saving an empty mask removes the existing region file. `removeAll()` deletes
only grammar-matched regular files two numeric bucket levels under the
validated layer directory, never follows a symlink, never calls `remove_all`,
and removes only bucket directories it emptied. The layer directory itself
being a symlink is a refusal, not a traversal.

`setPersistenceRoot(saveRoot) -> bool` fails for an invalid root and after the
field already holds a chunk, because a late root could shadow disk state. An
accepted root invalidates the gathered window, so a root set after frames have
rendered still uploads the saved regions on the next gather.
`flushToDisk() -> int` saves every persistence-dirty resident region; the
creation's save-all path calls it. `clear()` also removes the layer files and
drops all region records. Destruction never flushes.

One file per field chunk is rejected by the measurements in D12. A bucket
census removes absent probes but not the more expensive present loads. An
8x8-chunk region still needs 289 opens at the cap; a 32x32-chunk region makes a
dense file 1 MiB and rewrites it for one dirty cell. A layer-wide index adds a
second crash-consistency problem. Voxel `.vxs` files are 3D and cannot own an
independent 2D explored field; the entity snapshot would rewrite the whole
field.

## D11 — Region residency and probe discipline

With persistence, a region is resident or non-resident. A resident region has
loaded all chunks named by its file, or has recorded that the file was absent.
A non-resident region has no in-memory record.

The first CPU read or write in a non-resident region performs exactly one
`loadRegion` probe. Gather expansion of any chunk in that region does the same.
Loaded chunks enter through `assignChunk` as persistence-clean; a missing file
makes the region resident and empty. Reading every cell in one region therefore
performs one probe, not one probe per cell or field chunk.

A changed write marks the region persistence-dirty. A load does not. A write
always probes before modifying a non-resident region, so it cannot overwrite a
disk copy with a one-cell partial image. Every resident region also has an
access bit set by CPU access.

Eviction runs only from the gather and only when the window origin changes.
For each resident region outside the window's field-chunk rectangle plus four
margin chunks on every side, eviction saves it if dirty and drops it only when
its access bit is clear. It then clears all access bits. Consequently:

- a region is probed at most once per residency epoch;
- a far region read every frame remains resident while the camera moves;
- a static camera never evicts; and
- resident memory is bounded by the keep rectangle plus gameplay-kept regions:
  at most 16 dense regions (4 MiB) at `W = 1152`, and 100 (25 MiB) at the cap.

After a persistent `clear()`, the next whole-window gather probes every window
region once and quietly finds no file. Without persistence there are no region
records, probes, access bits or eviction.

Per-field-chunk residency and a durable negative cache are rejected because a
region already bounds both positive and negative probes. Deferred per-frame
loads are rejected for this phase: they add a visible deferred state and make
fixtures frame-count-dependent despite D12's measured bound. Evicting every
frame is rejected because a gameplay-hot far region would reload every frame.

## D12 — Cold-gather bounds, budgets and bail

For window edge `W`, the worst-case number of intersected regions per axis is:

```text
ceil((W / 32 - 1) / 16) + 1
```

A whole-window gather therefore probes the square of that count. It happens on
the first frame, after `clear()`, or after a move of at least `W / 32` field
chunks on either axis. A normal crossing opens new files only when it enters a
new region row or column, at most the per-axis count.

| `W` | Worst-case regions | Maximum probes per moved axis |
|---:|---:|---:|
| 1152 | 16 | 4 |
| 1472 | 16 | 4 |
| 1856 | 25 | 5 |
| 2496 | 36 | 6 |
| 3200 | 64 | 8 |
| 4096 | 81 | 9 |

The CPU-field phase runs a fresh field over an already populated save root for
all six window edges. Sparse rows assert that probes equal the region count,
loads are positive and saved cells read back; dense rows at 1152 and 4096 also
report elapsed time. On the author's Debug host those dense rows must remain at
or below 24 ms and 160 ms. Counts are the portable test oracle; elapsed time is
a reported bail criterion, not a `ctest` wall-clock assertion.

The window phase measures `IRPerfGrid` at 1280x720 Debug. Smooth persisted pan
keeps `fogWindowGather` max at or below 2 ms. A teleport over a dense save keeps
it at or below 33 ms with at most 16 probes and 16 loads per jump. A count over
the bound or a CPU-field dense row over budget blocks dependent work. A
teleport over budget first gets the reusable Metal staging buffer already
called for by the texture backend; if it still fails, asynchronous region I/O
requires a separate design.

### Persistence measurements behind the unit

The plan-time prototype used macOS 26.5 on an M4 Max with APFS, warm page
cache, two runs per row and the same file grammar. The per-field-chunk prototype
used `fopen`/`fread`/`fclose` on 1,100-byte files.

| `W` | Field chunks / opens | Empty root, `-O2` | Dense, `-O2` |
|---:|---:|---:|---:|
| 1152 | 1,296 | 3.6-4.4 ms | 14.7-15.6 ms |
| 1472 | 2,116 | 2.7-3.1 ms | 23.7-24.4 ms |
| 1856 | 3,364 | 4.2-5.9 ms | 39.0-39.4 ms |
| 2496 | 6,084 | 7.7-8.0 ms | 72.3-73.7 ms |
| 3200 | 10,000 | 12.5-12.7 ms | 124-139 ms |
| 4096 | 16,384 | 20.6-21.9 ms | 220-228 ms |

The selected 16x16-field-chunk region prototype used a 256-bit mask and packed
present chunks. Sparse means one chunk in sixteen present. Dense `-O0` includes
the same per-chunk non-zero recount `assignChunk` performs.

| `W` | Regions | Empty root | Sparse `-O2` | Dense `-O2` | Dense `-O0` | Enter region column, dense `-O2` / `-O0` |
|---:|---:|---:|---:|---:|---:|---:|
| 1152 | 16 | 0.05-0.12 ms | 0.27-0.34 ms | 0.78-0.85 ms | 8.8 ms | 0.18 / 1.9 ms |
| 1472 | 16 | 0.05-0.11 ms | 0.21-0.22 ms | 0.79-0.96 ms | 7.6 ms | 0.17-0.36 / 2.0 ms |
| 1856 | 25 | 0.07-0.14 ms | 0.34-0.53 ms | 1.5-1.7 ms | 11.8 ms | 0.23-0.42 / 2.0 ms |
| 2496 | 36 | 0.11-0.14 ms | 0.64-0.71 ms | 1.8-2.1 ms | 17.3 ms | 0.30 / 3.2 ms |
| 3200 | 64 | 0.23-0.27 ms | 0.90-0.91 ms | 3.4-3.9 ms | 27.3 ms | 0.36-0.40 / 3.4 ms |
| 4096 | 81 | 0.23-0.42 ms | 1.1-1.7 ms | 4.1-4.3 ms | 37.0 ms | 0.40-0.44 / 3.9 ms |

At `-O0`, expanding a whole RGBA8 window cost 2.5 ms at 1152 and 35 ms at
4096; at `-O2`, 0.07 ms and 7.5 ms. A dense 257 KiB region save through a
temporary file and rename cost 0.13-0.22 ms at `-O2`.

## D5 — Pending changes and window gather

The persistent and transient `ChunkedField2D` dirty-key lists form the pending
set. The gather is their only drain and calls `update()` once per frame per fog
component. Persistence-dirty remains a separate per-region property cleared on
save.

A GPU-free planner consumes the current texture origin (unset initially), the
new origin, `W` and pending keys. It returns field chunks to expand and
texture-space upload rectangles.

- First frame, `clear()` and moves of at least `W / 32` chunks on an axis
  re-expand the whole window.
- A smaller crossing expands only the newly exposed strip, at most one per
  moved axis.
- Pending chunks inside the window expand in place; pending chunks outside it
  are drained without upload.
- Rectangles are field-chunk-aligned and remain inside the texture. A wrapped
  strip splits into at most two rectangles. Pending chunks coalesce into one
  span per texture field-chunk-row run. A whole-window gather uploads in
  32-row strips.
- CPU scratch is one `32 * W * 4` strip retained as a system high-water buffer.

An unchanged origin with no pending keys performs no upload and no probe. A
crossing issues at most four strip uploads plus one per pending span and at
most D12's per-axis probes. A whole-window gather performs at most D12's region
count. An unconditional full upload, a full re-gather for every 32-column
crossing and one upload per field chunk are rejected.

## D6 — Camera-anchored GPU window

The centre is the world XY point at z = 0 under the fog canvas viewport centre
for the live effective camera, including visual yaw and pivot. It is computed
through a new `IRMath::pos2DIsoToPos3DAtZLevelYawed` helper, the exact fixed-z
inverse of the yawed projection. If `isoPixelToPos3DYawed` exists when the
window phase starts, the new helper is implemented alongside or in terms of
that inverse rather than establishing a competing convention. A raw camera
anchor is rejected because explicit pivots and origin-mode yaw move the
viewport centre away from it.

Let `C = (Cx, Cy)` be the fog canvas size, and let:

```text
R   = length((Cx / 2, Cy / 2)) / sqrt(2)
rho = R + sqrt(2) * 128
W   = ceilToMultiple(2 * (rho + 33), 64)
```

`W` is capped at `kFogWindowEdgeMax = 4096`. The 33 cells are 32 for chunk
snap and one for column rounding. A voxel at height `z` that draws anywhere on
the canvas has a column within `R + sqrt(2) * abs(z)` of the centre; rotation
preserves that length. The snapped window covers a Chebyshev radius of
`W / 2 - 33`, proving coverage for D10's slab until the cap applies.

| Game resolution | Canvas | `R` | Formula / `W` | RGBA8 |
|---|---|---:|---:|---:|
| 1280x720 | 642x722 | 341.6 | 1152 | 5.1 MiB |
| 1920x1080 | 962x1082 | 511.9 | 1472 | 8.3 MiB |
| 2560x1440 | 1282x1442 | 682.2 | 1856 | 13.1 MiB |
| 3840x2160 | 1922x2162 | 1022.8 | 2496 | 23.8 MiB |
| 5120x2880 | 2562x2882 | 1363.3 | 3200 | 39.1 MiB |
| 7680x4320 | 3842x4322 | 2044.5 | 4544 / 4096 cap | 64 MiB |

When capped, the uncovered periphery follows D10 and reads unexplored. Canvas
attachment logs one warning with the canvas size, formula result and covered
radius.

The origin is `snap32(round(centre)) - W / 2`. World column `c` lives at
texture texel `floorMod(c, W)`. A shader first proves
`0 <= c - origin < W` per axis, then adds `floorMod(origin, W)` and wraps once.
Shader modulo uses only non-negative operands. `W` comes from `imageSize`; no
shader constant describes the window.

Observer-tail lanes `pad1_` and `pad2_` become `windowOriginX_` and
`windowOriginY_`. The block size does not change. The gather writes the origin
before any observer upload, so texture contents and origin are from the same
frame. The gather remains inside `VOXEL_TO_TRIXEL_STAGE_1`, including both
existing call sites.

A fixed 256 window, power-of-two rounding, passing the modulo base in the only
two free UBO lanes, appending another UBO member, four cells per texel and a
separately registered gather system are rejected.

## D7 — Line-of-sight re-anchoring

Each per-source 256x256 line-of-sight tile is anchored on its source:
`roundHalfUp(sourceCentre) - 128`. CPU and shader derive the same origin from
the analytic circle centre already in the observer block, so no new UBO field
is needed. Sampling outside the tile remains unoccluded, and a disc wider than
128 cells is documented as clipped to the tile.

Each gated source has a co-anchored 256x256 column-top view. The component
retains at most eight views: `8 * 256 * 256 * sizeof(int32) = 2 MiB`. One pool
pass tests each live voxel against at most eight source boxes, and flagged
shape rasterization clips to the same boxes. `buildLosHorizons` traces a
source only through its matching column view, so moving the source moves both
the horizon tile and every occluder it can observe.

`IRPrefab::Fog::lineOfSight` follows the same convention independently: it
anchors its one reusable 256x256 query view at
`roundHalfUp(from.xy) - 128`, rasterizes that box, and returns unoccluded when
the target is outside it. `FogLineOfSightField::cellInField`, `horizonIndex`,
the CPU trace and both shader helpers therefore take or derive a tile origin;
no LOS path retains a world-centred half-extent test.

Growing every tile to `W` is rejected for its build and memory cost. Anchoring
tiles on the fog-window centre is rejected because off-centre sources lose
occlusion. One union column view is rejected because widely separated sources
make its bound either larger than the fixed tile contract or incomplete.

## D8 — Vision-source tier

The first `kMaxFogVisionCircles` sources added are analytic and evaluated per
pixel. Later sources stamp their XY discs into the transient field layer with
`fillRow`. A disc contains cells whose centres lie within its radius, matching
`revealRadius`.

`WorldField::getCell` and the gather take the maximum of persistent and
transient state. `clearVisionCircles` clears the transient layer. That layer
never persists, probes or evicts, and leaves no explored memory. It deliberately
does not provide analytic edge softness, height cost, line of sight or
channels; callers add their highest-priority sources first.

Raising the analytic cap is rejected because it changes every mirrored std140
block and remains a cap. Persisting tier stamps is rejected because a live
source expresses visibility now, not explored memory.

## Consumer audit

The current fixed-window convention has these direct consumers. The window
phase changes all of them together.

| Consumer | Window dependency |
|---|---|
| `engine/render/src/shaders/c_fog_to_trixel.glsl` | Paint lookup uses the fixed half-extent. |
| `engine/render/src/shaders/metal/c_fog_to_trixel.metal` | Metal twin of the paint lookup. |
| `engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl` | Stage-1 column keep/drop lookup. |
| `engine/render/src/shaders/metal/c_voxel_to_trixel_stage_1_body.metal` | Metal twin of the stage-1 lookup. |
| `engine/render/src/shaders/c_voxel_visibility_compact.glsl` | Compact-pass fog cull and 1x1 placeholder path. |
| `engine/render/src/shaders/metal/c_voxel_visibility_compact.metal` | Metal twin of the compact lookup. |
| `engine/render/src/shaders/ir_voxel_face_select.glsl` | Shared face-selection fog taps. |
| `engine/render/src/shaders/metal/ir_voxel_face_select.metal` | Metal twin of the shared taps. |
| `engine/render/src/shaders/ir_fog_los.glsl` | LOS tile bounds, local lookup and texture packing. |
| `engine/render/src/shaders/metal/ir_fog_los.metal` | Metal twin of the LOS lookup. |
| `test/render/shaders/c_fog_cross_section_probe.glsl` | Includes the real GLSL face-selection helper. |
| `engine/prefabs/irreden/render/fog_line_of_sight.hpp` | Column stamping, shape clip boxes, horizon traces and build bounds use the LOS tile origin. |
| `component_canvas_fog_of_war.hpp` — `FogLineOfSightField::cellInField` / `horizonIndex` | CPU visibility and horizon indexing use source-local tile coordinates. |
| `VOXEL_TO_TRIXEL_STAGE_1::gatherFogWindow` | Gathers before the per-canvas early return and again through the world-fog `beginTick` resolve; the second call finds nothing pending in the same frame. |
| `FOG_TO_TRIXEL` | Binds the same texture and observer block for paint. |
| `FOG_LOS_BUILD` | Builds each source's co-anchored column view and horizon tile, then uploads the packed LOS texture. |
| `IRPrefab::Fog::lineOfSight` | Anchors and fills the standalone query view before tracing to the target. |
| `test/render/fog_line_of_sight_test.cpp` | Pins LOS field edges, indexing, rasterization and horizon behavior. |
| `test/render/fog_cross_section_test.cpp` | Mirrors the fog-grid convention for the probe host and the LOS tile constants for the LOS arm. |

Fog-attached demo coverage is `fog_demo`, `perf_grid`, `lua_perf_grid`,
`skeletal_demo` and the `lighting/main_combined` configuration. Their existing
content lies inside the legacy window; the pre-existing reference rows are the
OFF-path parity gate while the field and moving-window phases land.

## D9 — Phase map

| Phase | Delivers | Depends on |
|---|---|---|
| Docs | This contract, the field-kit amendment, the superseded upload analysis and instruction cross-references. | None |
| CPU field | D1-D5, D11, D12's cold-gather arm and D3's world-space API, initially gathered into the legacy fixed texture. | Docs |
| Window | D6, D7, D10, D12's frame budgets, all shader consumers, eviction and visual/perf fixtures. | CPU field and the line-of-sight / field-paint layouts it composes with |
| Source tier | D8 and its positive-fire coverage. | Window |

The split keeps storage, persistence and cold-I/O proof GPU-free before the
shader convention changes. The window phase changes the out-of-window contract
and every shader tap atomically. The source tier then builds only on the
settled world field.
