# Plan — issue #2640

> Source: the `## Plan` comment on jakildev/IrredenEngine#2640
> (posted 2026-08-08), cleared by the opus plan-review pass the same day.
> Committed here per #1932 (the plan file lands as the first commit of the
> implementing PR). The plan review's two corrections and the 2026-09-10
> shape note are appended at the bottom.

## Plan: Chunked occupancy-field prefab + placement-query kit

- **Issue:** #2640
- **Model:** opus (per child — the decomposition below pins every design
  decision, so no child needs fable-class novel design)
- **Date:** 2026-08-07

### Verified current state

The "engine does not do this today" premise was source-verified tree-wide
(engine/ + creations/ + test/ + docs/, third_party and build dirs excluded),
not assumed:

- **No CPU distance transform**: `distance ?transform|felzenszwalb|\bEDT\b`
  → 0 hits; `chamfer` → 0. The only CPU grid-of-distances code is
  `IRMath::SDF::evaluateGrid` (`engine/math/include/irreden/math/sdf.hpp:182`)
  — analytic primitive sampling, no occupancy input.
- **No min-spacing sampler**: `poisson` → 0, `bridson` → 0, `blue.?noise` → 0.
- **No CCL / union-find**: `union.?find|disjoint.?set` → 0. The only real
  flood fill is a demo-local 4-connected plane fill in the voxel editor
  (`creations/editors/voxel_editor/main.cpp:938`). `percolation.hpp` is an
  8-line stub.
- **No clearance field**: `clearance` → 1 prose comment.
- **What does exist, and this kit must reconcile with**:
  - `IRPrefab::Spatial::SpatialGrid` + `IRSpatial.queryRadius`
    (`engine/prefabs/irreden/spatial/`, #1421) — a point-insert **entity**
    index ("which entities are near P"), with the caller-owned-out-buffer +
    touched-key-clearing allocation discipline ("Pattern B" in the
    *allocation* sense of `spatial_grid.hpp:13-20`, not the API-shape sense
    of `engine/prefabs/irreden/render/CLAUDE.md:276`). No per-cell value
    storage — it cannot represent a field.
  - Three incompatible ivec3→64-bit key packings:
    `IRPrefab::Chunk::pack` (int16/axis, 32-voxel lattice baked in,
    `engine/prefabs/irreden/world/chunk_coord.hpp:82`),
    `SpatialGrid::cellKey` (21-bit/axis raw, `spatial_grid.hpp:153`),
    `IRPrefab::Voxel::detail::packCellKey` (21-bit/axis biased,
    `face_occupancy.hpp:24`).
  - `ChunkResidencyManager` (`engine/world/`) — creation-constructed (not
    `World`-owned), no resident/evict hooks for a parallel data plane, and
    `docs/design/world-streaming.md` designs no scalar-field chunking or
    chunk-level query pruning (grep for field/prune/query/occupancy in that
    doc: 4 incidental lines). This kit is that open design space.
  - Per-chunk-summary prior art: the pool-slot `ChunkBounds` cache
    (`component_voxel_pool.hpp:28-49`, sanctioned dirty-flag pattern) — but
    its chunk axis is pool-slot index, not space; and
    `IRRender::kVoxelChunkSize = 256` is a GPU dispatch bucket unrelated to
    `IRConstants::kChunkSize` (the docs call out the name collision).
  - RNG: `IRMath::threadRng()` is `std::mt19937` behind thread-local state
    (`engine/math/src/ir_math.cpp:15`). The raw mt19937 word stream is
    standard-specified, but `std::uniform_*_distribution` is **not**
    implementation-portable, and thread-local state means determinism would
    couple to which thread runs the query — so the kit does not use it.
- **Consumer shape confirmed**: a creation's in-review nav implementation
  (the Lua-first probe the issue body references) lands exactly this query
  shape in creation C++ — per-chunk SoA passability + clearance bake,
  chunk-first pruning on per-chunk summaries, and a
  `{count, clearance, minSpacing, anchor-bias, seed}` → chunk-qualified
  positions query via seeded Bridson — and explicitly defers the general
  surface to this issue. Its EDT is a **global** pass (boundary-correct by
  construction); the genuinely new engine work is the *incremental*
  dirty-chunk recompute, which is only exactly correct with a capped field
  (decision D4 below).

No phase-0 probe is needed: no phase below gates on a runtime measurement —
the premises are the source-verified absences and API shapes above, which a
plan reviewer can re-verify by re-running the quoted greps.

### Scope

An epic, not one PR. This comment is the **umbrella plan**; on approval,
convert #2640 into the umbrella via `file-epic` (5 chained children, each
`Blocked by:` its predecessor, all engine-public — no downstream-repo
references in any child). Deliverable: a game-agnostic chunked 2D
occupancy/scalar field kit in `engine/prefabs/irreden/spatial/` +
`engine/math/`, with per-chunk summaries, capped incremental EDT clearance,
stitched region labels, deterministic Poisson-disk placement draw, a
docs-first contract doc, and gtest coverage in `IrredenEngineTest`.

### Approach

Committed design decisions (the contract child C1 turns these into the
reviewable doc; children C2–C5 implement them):

- **D1 — pure cell space, 2D, v1.** The kit operates on integer `ivec2`
  cells; world↔cell mapping, cell meaning, and passability semantics stay
  caller-owned (grid semantics legitimately differ per game). Params
  (clearance, spacing) are in cell units. 2D only in v1 — the one proven
  consumer shape is ground-plane placement; the doc records the 3D
  extension path (F–H is separable; storage gains an axis). No dimension
  template.
- **D2 — storage.** `ChunkedField2D<T>`: sparse
  `std::unordered_map<FieldChunkKey, Chunk>` of dense 32×32 cell chunks
  (`kFieldChunkEdge = 32`, compile-time, shift/mask indexing). Per-chunk
  summary: min, max, nonzero count, dirty flag; nonzero count maintained
  incrementally on `setCell`, min/max refreshed by whole-chunk passes.
  Allocation discipline is SpatialGrid's Pattern B: buckets retain
  capacity, queries write into caller-owned out-vectors.
- **D3 — key packing.** New 2D `FieldChunkKey` = two int32 halves of a
  `uint64_t` (x low, y high). None of the three existing packings is 2D;
  int32/axis removes the residency packing's ±32768 wrap caveat outright.
  The doc's "relationship to residency chunks" section states: field chunks
  tile **cell** space, residency chunks tile **voxel** space; same 32 edge
  for cognitive alignment; they coincide only when a creation defines
  1 cell = 1 voxel column. Field chunks do not touch `ChunkResidencyManager`
  (which offers no data-plane hook anyway); a residency-following field
  polls residency explicitly if a creation wants that.
- **D4 — clearance = capped integer squared EDT.** Clearance is stored as
  `int32` **squared** cell distance to the nearest occupied cell, saturated
  at a per-field `maxClearance` cap. All arithmetic is integer → bit-exact
  on every platform, no sqrt anywhere (queries compare `c² ≤ clearanceSq`).
  The cap is what makes incremental recompute *exact*, not approximate: an
  occupancy change can only affect clearance within `maxClearance` cells,
  so recompute runs Felzenszwalb–Huttenlocher (two separable 1-D passes)
  over **window = dirty chunks + 2·maxClearance-cell ring**, and writes
  back only **dirty chunks + 1·maxClearance-cell ring** (window cells
  outside the write-back ring are read-seed only). Full-field rebuild is
  the same code path with everything dirty. Boundary semantics: cells
  outside any present chunk are treated as **occupied** (conservative, per
  the issue).
- **D5 — region labels.** Per-chunk local CCL over free cells (4-connected,
  two-pass), then seam stitching: union-find over (chunk, local-label)
  pairs across the 4 chunk borders, resolved to global region ids.
  Incremental: relabel dirty chunks locally, then rebuild the union-find +
  global remap (O(#chunks + #seam segments) — cheap by construction).
  Global ids are **not stable across updates**; callers compare labels
  within one update epoch only (documented).
- **D6 — placement draw.** Bridson Poisson-disk, all-integer: candidates
  are integer cell offsets rejection-sampled from the annulus
  `[minSpacing, 2·minSpacing]` (no libm transcendentals anywhere in the
  draw path), background grid at `gridWidth(r) = max(1, isqrt(r²/2))` —
  exactly `floor(r/√2)`, computed in integers via a new `IRMath::isqrt`
  kernel — **not** a float `r·0.7071f` (a truncated decimal is not
  `1/√2` and undershoots at five in-domain values),
  spacing checks in integer squared distance. `minSpacing` is bounded
  **below by 1**: at 0 the annulus collapses to the anchor offset alone. Seeded at the anchor cell —
  early-out at K yields the near-anchor bias the consumer shape needs
  without a weight function (weighted bias recorded in the doc as a future
  extension, not built). Validity per candidate: cell present,
  `clearanceSq ≥ c²`, and (optional flag) same region label as the anchor.
  Fewer than K reachable candidates → returns what it found; caller reads
  `out.size()`. **K itself is bounded `[1, kMaxPlacementHits]`** — `k ≤ 0` is
  rejected like `minSpacing = 0`, because with no failure sentinel an accepted
  `k = 0` returns the same empty vector as a genuinely full field; the struct's
  `k_ = 0` default is out of domain on purpose, so "forgot to set K" is caught
  rather than silently answered. **`out` is cleared on entry, before
  validation** (matching the composing sibling `SpatialGrid::queryRadius`), so a
  rejected query leaves it empty and `out.size()` means "hits found this call" —
  which is what makes the `out.size() ≤ k` bound hold.
- **D7 — determinism.** The draw takes an explicit `uint64_t seed` and uses
  a kit-local `IRMath::Pcg32` (new, ~20-line header in `engine/math/`) —
  never `threadRng()` (thread-coupled) and never `std::*_distribution`
  (non-portable). Same seed + same field state ⇒ byte-identical results on
  every platform — the same hits, in the same order. A locked stream is
  necessary and **not sufficient**, so D7 also pins *which word goes where*:
  a multiply-shift `uniformBelow` (one word per bounded value, no rejection at
  that layer, so `%` is not a substitute), two words per annulus attempt
  (`dx` then `dy`, a rejected attempt consuming both), a uniform draw for the
  active-list index with swap-and-pop removal, `kPlacementAttempts = 30`,
  only-accepted-candidates-become-samples with the anchor as the one
  unconditional seed, and `out` in acceptance order. The skeleton's prologue is
  contract too: clear `out`, then validate, then seed the anchor — validating
  before seeding keeps a rejected query from mutating the active list and grid,
  and clearing before validating makes "empty `out`" the one post-state of every
  failed query under any spelling of "rejects". Plus the negative rule:
  nothing observable may derive from `unordered_map` iteration order — region
  ids are numbered in ascending packed `FieldChunkKey`, then row-major local
  index (D5). A reproducibility test locks the PCG32 stream itself; a
  committed reference hit list locks the order the stream is spent in.
- **D8 — composition + API shape.** `IRPrefab::Spatial::PlacementField`
  owns the three layers (occupancy `ChunkedField2D<uint8_t>`, clearanceSq
  `ChunkedField2D<int32_t>`, labels) + `update()` (EDT + relabel over the
  occupancy dirty set, then clear dirty). Free function
  `queryPlacements(const PlacementField&, const PlacementParams&,
  std::vector<PlacementHit>& out, PlacementQueryStats* stats = nullptr)`;
  `PlacementHit { ivec2 cell_; ivec2 chunk_; }`. Chunk-first pruning on
  `maxClearanceSq < c²`. The stats out-struct (chunks considered / pruned)
  is what makes pruning and cost-proportionality **observable** for the
  acceptance tests. Plain types + free functions only, like
  `SDF::evaluateGrid` — **no engine system, no component, no `SystemName`
  entry, no pipeline wiring** in v1: the proven consumer embeds the kit in
  its own bake system; the engine stays mechanism-only.
- **D9 — Lua exposure: defer, with rationale (explicit per acceptance).**
  No engine-level Lua binding in v1. The known consumers are creation C++
  systems that expose their own domain-specific Lua wrappers (exactly how
  the in-review consumer does it); binding the raw kit would lock names
  before the API has a second consumer, and the `IRSpatial` precedent
  (`queryAabb` deliberately unbound until a consumer exists,
  `lua_spatial_bindings.hpp:24`) points the same way. The doc records the
  intended future binding shape next to the `IRSpatial` conventions.
- **D10 — kernels in IRMath, composition in prefabs.** Field-layout-agnostic
  pieces (`Pcg32`, the 1-D squared-EDT pass over a span) land in
  `engine/math/` (header-only, gtest-covered per kernel); everything
  chunk-aware lives in `engine/prefabs/irreden/spatial/` (header-only, per
  prefab convention — no CMake registration needed).

Child breakdown (each `Blocked by:` its predecessor; all `[opus]`):

- **C1 — contract doc (docs-only).** `docs/design/chunked-field-placement-kit.md`
  written to the same register as `lua-world-space-neighbour-query.md`:
  D1–D10 as the source-of-truth contract, the residency-chunk relationship
  section, the three-packings survey, a "Migration status" section. Plus
  the addendum's discoverability deliverable: cross-reference from
  `engine/prefabs/irreden/spatial/CLAUDE.md` (and `engine/math/CLAUDE.md`),
  and one relationship line in `lua-world-space-neighbour-query.md`
  ("entities near P" ↔ "valid space near P").
- **C2 — storage.** `chunked_field.hpp` (`ChunkedField2D<T>`, summaries,
  dirty tracking, `FieldChunkKey`) + `test/ecs/chunked_field_test.cpp`.
- **C3 — clearance.** `IRMath` 1-D squared-EDT kernel +
  `field_clearance.hpp` (capped windowed F–H per D4) +
  `test/ecs/field_clearance_test.cpp`.
- **C4 — regions.** `field_regions.hpp` (per-chunk CCL + seam-stitch
  union-find per D5) + `test/ecs/field_regions_test.cpp`.
- **C5 — placement.** `IRMath::Pcg32` + `field_placement.hpp` (D6–D8:
  draw, composed `PlacementField`, query + stats) +
  `test/ecs/field_placement_test.cpp` (including the end-to-end
  acceptance fixture) + doc status flip in the C1 doc.

### Affected files

- `docs/design/chunked-field-placement-kit.md` — new (C1)
- `docs/design/lua-world-space-neighbour-query.md` — one relationship line (C1)
- `engine/prefabs/irreden/spatial/CLAUDE.md` — file-table + kit section (C1, C5)
- `engine/math/CLAUDE.md` — new-kernel entries (C3, C5)
- `engine/prefabs/irreden/spatial/chunked_field.hpp` — new (C2)
- `engine/prefabs/irreden/spatial/field_clearance.hpp` — new (C3)
- `engine/prefabs/irreden/spatial/field_regions.hpp` — new (C4)
- `engine/prefabs/irreden/spatial/field_placement.hpp` — new (C5)
- `engine/math/include/irreden/math/edt.hpp` — new 1-D kernel (C3)
- `engine/math/include/irreden/math/rng_pcg32.hpp` — new (C5)
- `engine/math/include/irreden/ir_math.hpp` — include the two new headers (C3, C5)
- `test/ecs/chunked_field_test.cpp`, `test/ecs/field_clearance_test.cpp`,
  `test/ecs/field_regions_test.cpp`, `test/ecs/field_placement_test.cpp`
  — new (C2–C5), beside the composing sibling `test/ecs/spatial_grid_test.cpp`
- `test/CMakeLists.txt` — add each new test file to the explicit
  `add_executable(IrredenEngineTest ...)` source list (C2–C5; a file not
  listed silently never builds)

### Acceptance criteria

All fixtures are the new gtest files themselves (created by their child —
none pre-exist), in `IrredenEngineTest`, runnable headless on every host.
Positive-fire throughout: each named check asserts an observable firing,
not a default-pass.

- **C2**: per-chunk nonzero/min/max match a brute-force recount after
  randomized `setCell` sequences (seeded); dirty set is exactly the
  mutated chunks; bucket capacity retained across `clear()` (Pattern B);
  negative-cell mapping — cells -33/-32/-1/0/31 map to the D2 chunk+local
  table, `setCell` at (-1,-1) reads back through chunk (-1,-1) local
  (31,31), and a truncating-division reference mapping is asserted to
  *differ* on the negative arm (the positive half-space alone cannot
  distinguish the two spellings); presence/`clear()` both halves — after
  `clear()` no touched key is present via lookup *and* iteration, and a
  refill reuses retained buffers with no new allocation; a present all-zero
  chunk is never evicted and still reads free; dirty lifecycle — no-op
  `setCell` does not dirty, a value change does, `clear()` of a live chunk
  reports that key dirty, `update()` clears the set, a later mutation
  re-dirties it.
- **C3**: the issue's named cross-boundary test — an occupied cell in a
  *neighboring* chunk within radius r shrinks clearance at this chunk's
  edge (asserted as a strict decrease vs. the empty-neighbor control);
  conservatism — an *absent* neighbor chunk clamps edge clearance exactly
  as an occupied one does; cap — clearanceSq saturates at
  `maxClearance²` on an empty field (asserted equal, not ≤); incremental ≡
  full — randomized mutation sequences (seeded), dirty-window recompute
  byte-equals a from-scratch rebuild; numeric domain (D4) — a field at
  `maxClearance = kMaxClearanceCells` (1024) saturates at exactly 1,048,576,
  construction accepts 1 and 1024 and rejects 0 and 1025 (both arms), and
  the 1-D pass over a window row longer than 46,340 cells equals an int64
  reference (the row length at which an int32 intermediate would overflow
  regardless of the clearance cap).
- **C4**: an L-shaped free region spanning ≥ 3 chunks gets one label; a
  wall splitting it yields two labels with the wall's chunks re-stitched
  correctly; incremental relabel ≡ full relabel over seeded mutations; the ids
  themselves pinned by value against a literal reference (equality-only
  assertions pass under any numbering, so they cannot see a discovery-order
  labelling that renumbers on the next standard library).
- **C5**: PCG32 stream locked against reference values; same seed ⇒
  byte-identical hit list across two independent field rebuilds; all
  pairwise hit distances ≥ minSpacing (squared-integer check); every hit
  satisfies `clearanceSq ≥ c²`; anchor-region flag ⇒ zero hits outside the
  anchor's region on a two-region fixture; K-shortfall — a fixture with
  fewer than K valid cells returns exactly the valid count; **pruning
  fires** — on a mostly-low-clearance fixture, `PlacementQueryStats`
  reports `chunksPruned > 0` and `chunksConsidered <` total resident
  chunks (the cost-proportionality observable); out-of-domain params
  rejected at each boundary — `minSpacing` 0, `minSpacing` 1025,
  `c` = maxClearance+1, `k` 0, `k` -1 and `k` = kMaxPlacementHits+1 rejected,
  adjacent in-domain `minSpacing` 1, 1024, maxClearance, `k` 1 and
  `k` = kMaxPlacementHits accepted (both arms), and a default-constructed
  `PlacementParams` asserted rejected (its `k_ = 0` is the mistake the default
  invites); caller-output discipline — `out` pre-seeded with sentinels holds
  none of them after a successful query, after a rejected one, and after a
  query that finds nothing, and pre-seeding with `k` sentinels does **not**
  suppress the draw (the arm that fails when `out.size()` is read as a progress
  counter on an uncleared vector); no result exceeds K — `out.size() ≤ k` on the
  shortfall and reach-K fixtures, plus `k = 1` over a valid anchor giving
  exactly one hit and `candidatesDrawn_ == 0` (a bound alone passes on an
  implementation that draws the whole frontier and truncates);
  background-grid width pinned by value — `gridWidth(r)` equals a
  `floor(r/sqrt(2))` reference (the test may use libm; the kit may not) for
  every `r` in `[1, 1024]`, clamps to 1
  at `r = 1`, and gives `{239, 408, 478, 647, 717}` at
  `r ∈ {338, 577, 676, 915, 1014}` — the inputs where a truncated
  `0.7071` constant is one cell short, so that form fails;
  draw order pinned against a committed reference (D7) — the complete `out`
  (every cell/chunk pair, in order) and `candidatesDrawn_` asserted against
  literal reference values on a deterministically-built fixture, plus
  must-differ arms for a `word % n` range map and a LIFO active-list
  selection, since two rebuilds of one binary cannot observe a
  platform-varying order;
  end-to-end — build
  occupancy → `update()` → query returns K chunk-qualified hits honoring
  clearance + spacing + region + anchor bias under a fixed seed.

### Gotchas

- `std::uniform_*_distribution` is not portable across standard libraries —
  the kit maps raw PCG32/mt19937 words itself (D7). No libm (`sin`/`cos`)
  in the draw path — rejection-sample the annulus (D6).
- The squared representation has a **bounded domain**:
  `kMaxClearanceCells = 1024` caps `maxClearance`, `minSpacing` and the
  query radius `c`, because int32 `n²` overflows at n = 46,341.
  `minSpacing`'s domain starts at **1**, not 0 (a zero annulus cannot
  leave the anchor); `c`'s starts at 0, which is a real relaxation.
  Separately, every EDT intermediate is int64 — the F-H
  parabola term `f[q] + q²` is bounded by the *window row length*, not by
  the clearance cap, so the cap alone does not make the pass safe. Seeding
  free cells with `INT32_MAX` is the classic form of this bug.
- The D4 ring widths are the classic off-by-one: compute window =
  2·maxClearance ring, write-back = 1·maxClearance ring. Writing back the
  full window corrupts apron cells whose true nearest obstacle lies
  outside the window.
- `IRRender::kVoxelChunkSize` (256, GPU pool bucket) is unrelated to
  `IRConstants::kChunkSize` (32³ world cube) — the doc and code comments
  must keep "field chunk" distinct from both.
- "Pattern B" is overloaded in-tree (allocation discipline vs API shape) —
  say *allocation* Pattern B, citing `spatial_grid.hpp:13`.
- cpp-math rule: no `glm::*`/`std::` math outside `engine/math/` — kit code
  in prefabs uses `IRMath::` wrappers; `test/**` is outside the rule's
  scope and may use `std::` freely.
- Prefab headers are header-only with no CMake registration; the *tests*
  are the opposite — each new `.cpp` must be added to `test/CMakeLists.txt`
  explicitly or it silently never builds.
- Region labels are epoch-scoped (D5) — any consumer caching a label
  across `update()` is a bug; the doc must state this loudly.
- Engine repo is public: children and the doc must describe the consumer
  abstractly ("a creation's nav implementation") — no downstream-repo
  issue/PR numbers, paths, or feature names.

### Sibling / in-flight reconciliation

- **In-flight set as of 2026-09-10**, scoped to the directories the children
  actually write (`test/ecs/`, per correction 2 — not the `test/spatial/` an
  earlier scoping named, where the answer is trivially "none"). No open engine
  PR touches `engine/prefabs/irreden/spatial/` or `engine/math/`. Two touch
  `test/ecs/`: #3157 and #3155, each adding new test files **and** editing
  `test/CMakeLists.txt`'s explicit `add_executable(IrredenEngineTest ...)`
  source list, which C2–C5 also edit one line each. Same shape as correction
  1's `ir_math.hpp` collision — mechanical, expected on rebase, no design
  overlap (no shared file beyond the source list; the new filenames are
  distinct). A snapshot either way: C2–C5 re-derive their own in-flight set at
  claim time.
- #2564 (entity-anchored fog reveal, in planning concurrently) lives in
  the render fog surface (`C_CanvasFogOfWar`) — no file overlap; its cell
  grid is a *potential future consumer* of `ChunkedField2D`, not a
  dependency, and neither plan blocks the other.
- The in-review creation-side nav implementation is the consumer proof,
  not a conflict: it stays on its own grid types and swaps its query
  internals onto this kit in a later migration of its own (per the issue's
  "Why engine, not per-creation").
- `SpatialGrid`/`IRSpatial` (#1421) is the composing sibling — this plan
  adds fields/space queries beside it and cross-links the two docs (C1);
  no shared code paths change.


---

## Plan-review corrections (opus-reviewer, 2026-08-08) — folded into the children

1. **In-flight claim was not quite right.** PR #2850 (then WIP,
   `fleet:design-blocked`) touched `engine/math/include/irreden/ir_math.hpp`,
   the file C3 and C5 both edit to add includes — a mechanical include-list
   collision to expect on rebase, not a design conflict.
   **Status 2026-09-10: #2850 is MERGED, so the collision is retired.** C3/C5
   re-derive their own in-flight set at claim time rather than inheriting this
   line.
2. **Test directory placement — applied above, not pending.** The kit's tests
   go in **`test/ecs/`**, beside the sibling they compose with
   (`test/ecs/spatial_grid_test.cpp`, registered at `test/CMakeLists.txt:43`);
   a second top-level home for "spatial" tests costs a future reader a tree
   survey. The child breakdown and affected-files list above are written in
   that spelling directly, so there is one current instruction and nothing to
   translate at pickup time. Recorded here for the reasoning, not as pending
   work: a correction note is not a safe thing to copy deliverables from, so
   the literals it corrects do not get to stay standing beside it.

Small note for C5 (not a gap): the acceptance bullet "honoring … anchor bias"
is the one criterion that is not independently falsifiable — Bridson's
active-list frontier is only approximately radial, so under a fixed seed that
bullet re-asserts determinism. D6 defers weighted bias deliberately; do not let
that bullet read as a measured property of the bias.

## Shape note (2026-09-10, on release from the retired `human:review-plan` hold)

The plan is an **umbrella** (C1–C5, each blocked by its predecessor), not one
task:

- Land **C1** (the contract doc) as the first PR with `Refs #2640`.
- File **C2–C5** as agent-approved follow-ups chained by `**Blocked by:**`.
- Close #2640 with **C5**.
- Do not attempt a single PR.

### As filed (2026-09-10)

| Child | Issue | Blocked by | Deliverable |
|---|---|---|---|
| C1 | — (this PR) | — | contract doc + cross-references + this plan file |
| C2 | #3160 | #3159 | `chunked_field.hpp` — storage, summaries, dirty set (D1–D3, D10) |
| C3 | #3161 | #3160 | `edt.hpp` + `field_clearance.hpp` — capped windowed EDT (D4, D10) |
| C4 | #3162 | #3161 | `field_regions.hpp` — per-chunk CCL + seam stitch (D5) |
| C5 | #3163 | #3162 | `rng_pcg32.hpp` + `field_placement.hpp` — draw + query (D6–D9); **carries `Closes #2640`** |

Each child is `fleet:task` + `fleet:agent-approved` (approval-equivalent, so no
second human triage pass) and carries its plan in the issue body rather than a
`## Plan` comment — the decisions were already pinned by the umbrella plan above
and re-stated per child, so there is nothing left for a planner to derive.

**Blocker form.** Every `**Blocked by:**` is a bare `#N`, including C2's
reference to the C1 *pull request*. A raw PR URL is gated by `fleet-claim`
(which greps `pull/\d+` out of the field) but is invisible to the scout, whose
`_BLOCKER_REF_RE` matches `#N` only — so the URL form would gate the claim while
projecting the issue as *available* in the queue. Both tools resolve a PR number
through `gh issue view`; a merged PR reports `MERGED`, which is in both accept
sets. Verified against merged PR #2850 before filing.
