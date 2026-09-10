# Chunked occupancy-field + placement-query kit

- **Status:** contract locked (this doc), implementation staged — see
  "Migration status" below. Every decision `D1`–`D10` here is the
  source-of-truth; code that disagrees with this file is the bug.
- **Owning subsystem:** `engine/prefabs/irreden/spatial/` (chunk-aware
  composition) + `engine/math/` (layout-agnostic kernels). **Not** engine core,
  **not** an ECS system — see D8.
- **Tracking:** #2640 (umbrella).
- **Sibling contract:** [`lua-world-space-neighbour-query.md`](lua-world-space-neighbour-query.md)
  — "which *entities* are near P". This doc is "where is valid *space*". They
  compose; neither subsumes the other (see "Relationship to `IRSpatial`").

---

## The invariant this establishes

> A consumer that needs **valid positions** — "find K cells with clearance ≥ c,
> spacing ≥ s, in the same region as anchor A, near A" — obtains them from a
> **chunked field with per-chunk summaries**, queried so that cost is
> proportional to the chunks it *touches* plus the candidates it *draws*, never
> to world area. It does **not** materialize an obstacle table over the whole
> grid, does **not** run an O(r²) clearance kernel per cell at query time, and
> does **not** sort every candidate in the world to pick K.

The anti-pattern is specific and it has been hand-rolled repeatedly downstream:
a full-grid scan that (1) builds an occupancy table for the entire grid, (2)
evaluates a radius-r clearance kernel at every cell, (3) sorts all candidates,
(4) greedily rejects for spacing. Steps 1–3 are what the chunk summaries and the
precomputed clearance field exist to delete; step 4 is what the Poisson-disk
draw replaces.

The *semantics* of a grid — what a cell means, what "passable" means, what bias
a game wants — legitimately differ per game and stay caller-owned (D1). The data
structure and the query algorithms do not differ, and are the engine's to own.

---

## What already ships (and what does not)

Verified tree-wide (`fleet-rules-sweep`, git's own file matcher, 4269 files
swept, with a live positive control) at 2026-09-10:

**Does not exist anywhere in tree:**

| Capability | Sweep | Hits |
|---|---|---|
| CPU distance transform | `[Dd]istance[ _]?[Tt]ransform`, `[Ff]elzenszwalb`, `chamfer` | 0 |
| Min-spacing / blue-noise sampler | `[Pp]oisson`, `[Bb]ridson` | 0 |
| Connected-component labelling / union-find | `[Uu]nion[-_ ]?[Ff]ind`, `[Dd]isjoint[-_ ]?[Ss]et` | 0 |
| Clearance field | `clearance` | 1 prose comment (`creations/demos/detached_probe/main.cpp:142`) |
| (positive control) | `SpatialGrid` | 24 in 5 files — the sweep can find things |

**Does exist, and this kit must reconcile with it:**

- **`IRPrefab::Spatial::SpatialGrid`** (`engine/prefabs/irreden/spatial/spatial_grid.hpp`,
  #1421) — a point-insert **entity** index. No per-cell value storage: it
  cannot represent a field. It is the composing sibling, not a substitute.
- **`IRMath::SDF::evaluateGrid`** (`engine/math/include/irreden/math/sdf.hpp:182`)
  — the only CPU grid-of-distances code in tree. It samples an *analytic
  primitive* over a grid; it takes no occupancy input and computes no distance
  transform. Its shape (plain types, free function, caller-owned output span) is
  nonetheless the API precedent this kit follows (D8).
- **`IRPrefab::Chunk`** (`engine/prefabs/irreden/world/chunk_coord.hpp`) and
  **`IRWorld::ChunkResidencyManager`** (`engine/world/include/irreden/world/chunk_residency.hpp`)
  — voxel-space chunk identity and the streaming resident-set. See
  "Relationship to residency chunks".
- **The voxel pool's `ChunkBounds` cache**
  (`engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp:28`, guarded
  by the pool's `m_chunkBoundsDirty` flag) — the in-tree precedent for a
  per-chunk summary rebuilt only when a dirty flag says so. Its "chunk" axis is
  a pool-slot index, not space, and its summary is iso-space bounds rather than
  a value range, so the pattern transfers but the type does not.
- **`IRMath::threadRng()`** (`engine/math/src/ir_math.cpp:18`) — `std::mt19937`
  behind `thread_local` state. Deliberately **not** used by this kit (D7).
- `engine/math/include/irreden/math/percolation.hpp` is an 8-line TODO stub. It
  is not prior art for anything here.

---

## The contract

### D1 — pure cell space, 2D in v1

The kit operates on integer `IRMath::ivec2` **cells**. World↔cell mapping, what
a cell means, and what counts as occupied are **caller-owned**. Parameters
(clearance, spacing) are in cell units.

v1 is 2D only: the proven consumer shape is ground-plane placement. The 3D
extension path is real but unbuilt — Felzenszwalb–Huttenlocher is separable per
axis, so 3D adds one more 1-D pass and one more axis of chunk storage. **No
dimension template in v1** — a `ChunkedField<N, T>` written before a 3D consumer
exists would lock a shape nobody has exercised.

### D2 — storage

`ChunkedField2D<T>`: a sparse `std::unordered_map<FieldChunkKey, Chunk>` of
**dense 32×32 cell chunks** (`kFieldChunkEdge = 32`, compile-time, so cell↔chunk
indexing is shift/mask, never divide).

Per-chunk summary, maintained beside the cells:

| Field | Maintenance |
|---|---|
| `min_`, `max_` | refreshed by whole-chunk passes (a `setCell` cannot cheaply repair a `min`) |
| `nonZeroCount_` | incremental on every `setCell` |
| `dirty_` | set on any `setCell` that changes a value; cleared by `update()` |

Allocation discipline is `SpatialGrid`'s **allocation Pattern B**
(`spatial_grid.hpp:13-20`): buckets retain capacity across rebuilds, `clear()`
empties touched chunks without freeing them, and queries write into
**caller-owned** out-vectors. ("Pattern B" is overloaded in tree — the *API
shape* sense in `engine/prefabs/irreden/render/CLAUDE.md` is a different thing.
Always say *allocation* Pattern B and cite `spatial_grid.hpp:13`.)

### D3 — key packing

`FieldChunkKey` is a `std::uint64_t` holding **two int32 halves** — x in the low
32 bits, y in the high 32. This is a fourth packing in tree and that is
deliberate: none of the existing three is 2D, and int32-per-axis removes the
residency packing's ±32768 wrap caveat outright.

The three existing packings, for the reader who wonders why not one of them:

| Packing | Layout | Why not reusable here |
|---|---|---|
| `IRPrefab::Chunk::pack` (`chunk_coord.hpp:82`) | three int16 axes, 16-bit strides | 3D; bakes the 32-voxel lattice into chunk identity; wraps past ±32768 |
| `SpatialGrid::cellKey` (`spatial_grid.hpp:153`) | three raw 21-bit axes | 3D; folds (wraps) out of range — safe there only because a geometric filter re-rejects, which a field lookup has no equivalent of |
| `IRPrefab::Voxel::detail::packCellKey` (`face_occupancy.hpp:24`) | three biased 21-bit axes | 3D; file-private detail of the face-occupancy pass |

A wrap in a *field* key is not benign the way it is in `SpatialGrid`: there is no
downstream geometric filter to catch it, so two distinct chunks colliding on one
key would silently return another chunk's cells. int32/axis puts the wrap
boundary at ±2^31 cells, past any addressable world.

### D4 — clearance is a capped integer squared EDT

Clearance is stored as `std::int32_t` **squared** cell distance to the nearest
occupied cell, **saturated at `maxClearance²`** (a per-field cap in cells).

- **All integer, no `sqrt`, ever.** Queries compare `c*c <= clearanceSq`.
  Felzenszwalb–Huttenlocher's two separable 1-D passes are exact for squared
  Euclidean distance, so the whole pipeline is bit-identical on every platform
  with no float determinism question to answer.
- **Boundary semantics.** A cell outside any present chunk is treated as
  **occupied** (conservative, per the issue): a field never reports clearance it
  cannot vouch for. Non-resident and never-created are the same thing to the
  field — it has no residency opinion (D3).
- **The cap is what makes incremental recompute exact, not approximate.** This
  is the load-bearing claim, so here is the derivation:
  - *Adding* an obstacle at `p` can only **lower** clearance for cells within
    `maxClearance` of `p`. A cell farther than the cap already reads
    `maxClearance²` and still does.
  - *Removing* an obstacle at `p` can **raise** clearance for cells within
    `maxClearance` of `p`. Computing those exactly requires every obstacle
    within `maxClearance` of *them* — i.e. within `2·maxClearance` of `p`.
  - Therefore: run F–H over **window = dirty chunks + a `2·maxClearance`-cell
    ring**, and **write back only dirty chunks + a `1·maxClearance`-cell ring**.
    Window cells outside the write-back ring are **read-seed only**.
  - The window's own edge is safe: every obstacle that could set a write-back
    cell's value below the cap lies inside the window by construction, and cells
    beyond the window are seeded as absent-therefore-occupied, so no write-back
    cell can come back with an inflated clearance.
- **A full rebuild is the same code path** with every chunk marked dirty. There
  is no second implementation to drift.

> **The classic off-by-one lives here.** Window ring = `2·maxClearance`;
> write-back ring = `1·maxClearance`. Writing back the whole window corrupts
> apron cells whose true nearest obstacle lies outside it. The C3 test suite
> asserts incremental ≡ full over seeded random mutation sequences precisely to
> pin this.

### D5 — region labels

Per-chunk local connected-component labelling over **free** cells (4-connected,
classic two-pass), then **seam stitching**: a union-find over
`(chunk, local-label)` pairs across each chunk's 4 borders, resolved to global
region ids. This is the tiled-navmesh model.

Incremental update relabels only dirty chunks locally, then rebuilds the
union-find and the global remap — `O(#chunks + #seam segments)`, cheap by
construction, so there is no incremental-stitch subtlety to get wrong.

> **Global region ids are NOT stable across `update()`.** They are epoch-scoped.
> A consumer that caches a label across an update and compares it to a fresh one
> is a bug. Compare labels only within one update epoch.

### D6 — placement draw

Bridson Poisson-disk sampling, all-integer:

- Candidates are integer cell offsets **rejection-sampled from the annulus**
  `[minSpacing, 2·minSpacing]` — no `sin`/`cos`, no libm anywhere in the draw
  path.
- Background acceleration grid at `max(1, floor(minSpacing * 0.7071f))` (the
  `r/√2` cell that makes each grid cell hold at most one sample).
- Spacing checks in integer squared distance.
- **Seeded at the anchor cell**, with an early-out at K. That early-out is where
  the near-anchor bias comes from — Bridson's active-list frontier grows
  outward, so stopping at K yields anchor-proximate results without a weight
  function. Weighted bias is a recorded future extension, **not built**.
- Per-candidate validity: the cell is present, `clearanceSq >= c*c`, and
  (optional flag) its region label equals the anchor's.
- Fewer than K reachable valid candidates ⇒ the query returns what it found. The
  caller reads `out.size()`; there is no failure sentinel.

> Bridson's frontier is only *approximately* radial. "Honours anchor bias" is
> therefore not an independently falsifiable property under a fixed seed — the
> test that asserts it is really re-asserting determinism. Do not let a future
> reader take it for a measured guarantee.

### D7 — determinism

The draw takes an explicit `std::uint64_t seed` and uses a kit-local
`IRMath::Pcg32` (a new ~20-line header in `engine/math/`).

- **Never `IRMath::threadRng()`** — it is `thread_local`, so results would
  couple to which worker thread ran the query.
- **Never `std::uniform_*_distribution`** — the raw `mt19937` word stream is
  standard-specified, but the distributions are *not* implementation-portable.
  The kit maps raw PCG32 words to ranges itself.

Same seed + same field state ⇒ **byte-identical** results on every platform. The
C5 suite locks the PCG32 stream itself against reference values, so a stream
change is caught as a stream change rather than as a mysterious placement diff.

### D8 — composition and API shape

```cpp
namespace IRPrefab::Spatial {

struct PlacementHit {
    IRMath::ivec2 cell_;    // world cell
    IRMath::ivec2 chunk_;   // the field chunk it came from
};

struct PlacementQueryStats {
    int chunksConsidered_ = 0;
    int chunksPruned_     = 0;
    int candidatesDrawn_  = 0;
};

class PlacementField {                       // owns the three layers
    ChunkedField2D<std::uint8_t> occupancy_;
    ChunkedField2D<std::int32_t> clearanceSq_;
    /* region labels */
  public:
    void update();   // EDT + relabel over the occupancy dirty set, then clear it
};

void queryPlacements(
    const PlacementField &field,
    const PlacementParams &params,
    std::vector<PlacementHit> &out,
    PlacementQueryStats *stats = nullptr);

} // namespace IRPrefab::Spatial
```

- **Chunk-first pruning**: a chunk whose summary `max_ < c*c` in the clearance
  layer cannot contain a valid cell and is skipped without touching a cell.
- **The stats out-struct is not a nicety** — it is what makes pruning and
  cost-proportionality *observable*, and therefore testable. Without it, "cost
  is proportional to touched chunks" is an unfalsifiable claim.
- **No engine system, no component, no `SystemName` entry, no pipeline wiring in
  v1.** Plain types and free functions, like `SDF::evaluateGrid`. The proven
  consumer shape embeds the kit inside its own bake system; the engine ships
  mechanism, not policy. (Contrast `BUILD_SPATIAL_INDEX`, which *is* a system —
  because an entity index must be rebuilt from live archetype state every frame.
  A field is caller-mutated, so it has nothing to rebuild from.)

### D9 — Lua exposure: deferred, with rationale

**No engine-level Lua binding in v1.**

The known consumers are creation C++ systems that expose their own
domain-specific Lua wrappers over their own grid semantics. Binding the raw kit
would lock names — cell coords, clearance units, region-id lifetime — before the
API has a second consumer to generalize against, and D5's epoch-scoped region
ids are exactly the kind of invariant that is easy to violate from script.

The precedent points the same way: `IRSpatial` binds `queryRadius` only and
leaves `queryAabb` deliberately unbound until a consumer exists
(`engine/script/include/irreden/script/lua_spatial_bindings.hpp`).

When a second consumer arrives, the intended shape is an `IRField` table beside
`IRSpatial`, returning an array of `{x, y}` records from `queryPlacements` —
positions **inline**, matching the `IRSpatial` convention that keeps the
per-candidate foreign-read footgun unreachable from script.

### D10 — kernels in IRMath, composition in prefabs

| Lives in | What |
|---|---|
| `engine/math/` | field-layout-agnostic kernels: `Pcg32`, the 1-D squared-EDT pass over a `std::span`. Header-only, gtest-covered per kernel. |
| `engine/prefabs/irreden/spatial/` | everything chunk-aware: storage, windowed EDT driver, region stitching, the draw, the query. Header-only per prefab convention — no CMake registration. |

The split is the reusability line: a 1-D squared-EDT pass over a span is useful
to anything with a scanline; a `2·maxClearance` write-back ring is meaningless
outside this kit.

---

## Relationship to residency chunks

The engine already has a 32-edge chunk convention, and this kit adds a second
one. That is intentional, and here is the exact relationship so no future reader
has to reverse-engineer it:

| | Residency chunk | **Field chunk** | Render voxel chunk |
|---|---|---|---|
| Symbol | `IRConstants::kChunkSize` (`ir_constants.hpp:20`) | `kFieldChunkEdge` | `IRRender::kVoxelChunkSize` (`ir_render_types.hpp:1690`) |
| Value | `32³` | `32²` | `256` |
| Tiles | **voxel** space (3D) | **cell** space (2D) | nothing spatial — a GPU dispatch/pool bucket |
| Key | `IRPrefab::Chunk::ChunkKey` (3× int16) | `FieldChunkKey` (2× int32) | n/a |
| Owner | `ChunkResidencyManager` | the `PlacementField` itself | the voxel pool |

- **Field chunks and residency chunks coincide only** when a creation defines
  1 cell = 1 voxel column. The kit does not assume that and does not require it;
  the shared edge of 32 is for cognitive alignment, not for identity.
- **Field chunks do not touch `ChunkResidencyManager`.** They cannot: the
  manager exposes no resident/evict notification hook for a parallel data plane
  (its public surface is `beginFrame`/`tickPrefetch`/`flushUploads`/`endFrame`
  plus `requestResident`/`requestEvict`/`isResident`/`forEachChunk` — frame
  hooks and queries, no observers), and it is constructed by streaming creations
  rather than owned by `World`.
- **A residency-following field polls, explicitly.** A creation that wants its
  field to track streaming calls `isResident(key)` / `forEachChunk` itself and
  mutates the field accordingly. Making that automatic would mean the engine
  minting a residency observer surface, which is a separate design (and belongs
  to [`world-streaming.md`](world-streaming.md), which today designs no scalar-
  field chunking or chunk-level query pruning at all — this kit is that open
  space).
- **`IRRender::kVoxelChunkSize` is not a chunk in the spatial sense** and the
  name collision is pre-existing —
  [`world-streaming.md`](world-streaming.md) §Topic 4 calls it out and settles
  it ("in the streaming code 'chunk' always means `kChunkSize`-cube, and the GPU
  dispatch bucket retains the name `kVoxelChunkSize`"). Code and comments in
  this kit say **"field chunk"** explicitly, never bare "chunk".

## Relationship to `IRSpatial`

[`lua-world-space-neighbour-query.md`](lua-world-space-neighbour-query.md) locks
the **entity** side: `IRSpatial.queryRadius` answers *"which entities are near
P"*, from a world-space index rebuilt once per frame by `BUILD_SPATIAL_INDEX`.

This kit answers *"where is valid space near P"*. The two compose and neither
subsumes the other:

- Spacing against **already-placed entities** is a `queryRadius` job — feed its
  hits into the field as transient occupancy before `update()`, or reject draw
  candidates against them.
- Spacing against **other cells drawn in the same query** is the Poisson-disk
  draw's job (D6), and no entity index can answer it, because those entities do
  not exist yet.

Rules of thumb: entities exist and move ⇒ `IRSpatial`. Cells are static between
edits and carry a value ⇒ this kit.

---

## Migration status

| Child | Deliverable | Status |
|---|---|---|
| **C1** | this doc + the cross-references (`engine/prefabs/irreden/spatial/CLAUDE.md`, `engine/math/CLAUDE.md`, the relationship line in `lua-world-space-neighbour-query.md`) | **landing** |
| **C2** (#3160) | `chunked_field.hpp` — `ChunkedField2D<T>`, summaries, dirty tracking, `FieldChunkKey` (D2, D3) | not started |
| **C3** (#3161) | `IRMath` 1-D squared-EDT kernel + `field_clearance.hpp` — capped windowed F–H (D4, D10) | not started |
| **C4** (#3162) | `field_regions.hpp` — per-chunk CCL + seam-stitch union-find (D5) | not started |
| **C5** (#3163) | `IRMath::Pcg32` + `field_placement.hpp` — draw, `PlacementField`, `queryPlacements` + stats (D6, D7, D8); flips this table to shipped | not started |

Each child is `**Blocked by:**` its predecessor. Tests live in **`test/ecs/`**,
beside `spatial_grid_test.cpp` — the kit's composing sibling — and every new
`.cpp` must be added explicitly to the `add_executable(IrredenEngineTest …)`
list in `test/CMakeLists.txt` (it is an explicit source list, not a glob; a file
that is not listed silently never builds).

## What to verify

The acceptance bar per child, phrased as observable firings rather than
default-passes:

- **C2** — per-chunk `nonZeroCount_`/`min_`/`max_` match a brute-force recount
  after seeded randomized `setCell` sequences; the dirty set is *exactly* the
  mutated chunks; bucket capacity survives `clear()` (allocation Pattern B).
- **C3** — an occupied cell in a **neighbouring** chunk within radius r shrinks
  clearance at this chunk's edge (asserted as a strict decrease against an
  empty-neighbour control, not merely as a bound); an **absent** neighbour chunk
  clamps edge clearance exactly as an occupied one does (the conservatism rule);
  `clearanceSq` saturates *equal to* `maxClearance²` on an empty field;
  incremental recompute **byte-equals** a from-scratch rebuild over seeded
  mutation sequences.
- **C4** — an L-shaped free region spanning ≥3 chunks gets one label; a wall
  splitting it yields two, with the wall's chunks re-stitched correctly;
  incremental relabel ≡ full relabel over seeded mutations.
- **C5** — the PCG32 stream is locked against reference values; same seed ⇒
  byte-identical hit lists across two independent field rebuilds; all pairwise
  hit distances ≥ `minSpacing` (integer squared check); every hit satisfies
  `clearanceSq >= c*c`; the anchor-region flag yields zero hits outside the
  anchor's region on a two-region fixture; a K-shortfall fixture returns exactly
  the valid count; **pruning fires** — `PlacementQueryStats` reports
  `chunksPruned_ > 0` and `chunksConsidered_ <` the resident chunk total on a
  mostly-low-clearance fixture; and one end-to-end fixture builds occupancy →
  `update()` → query and gets K chunk-qualified hits honouring clearance,
  spacing, region and anchor under a fixed seed.

Cross-cutting, for any reviewer of C2–C5:

- No `glm::*` and no `std::` math outside `engine/math/` (`.claude/rules/cpp-math.md`);
  kit code in prefabs goes through `IRMath::`. `test/**` is outside that rule's
  scope.
- No `sqrt` and no libm transcendental on any path in D4 or D6.
- No `std::uniform_*_distribution` anywhere (D7).

## References

- Felzenszwalb & Huttenlocher, *Distance Transforms of Sampled Functions* (2012)
  — the separable O(n) squared-EDT this kit's D4 kernel implements.
- Bridson, *Fast Poisson Disk Sampling in Arbitrary Dimensions* (2007) — D6.
- FIESTA / VDB-EDT — the incremental-distance-field prior art D4's windowed
  recompute follows.
- HPA\* / HNA\* hierarchical search — the coarse-first pruning D8 mirrors at
  chunk granularity.
- O'Neill, *PCG: A Family of Better Random Number Generators* (2014) — D7.
- In-tree: `engine/prefabs/irreden/spatial/spatial_grid.hpp` (allocation Pattern
  B, the composing sibling), `engine/math/include/irreden/math/sdf.hpp:182`
  (API-shape precedent), `engine/prefabs/irreden/world/chunk_coord.hpp`,
  `engine/world/include/irreden/world/chunk_residency.hpp`,
  `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp:28`
  (per-chunk-summary-behind-a-dirty-flag precedent).
