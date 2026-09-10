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

**Cell → chunk is floor division; shift/mask is how it is spelled.** Cells are
signed (D1, D3), so the mapping has to be stated rather than left to the
reader's default:

```cpp
constexpr int kFieldChunkEdge      = 32;
constexpr int kFieldChunkShift     = 5;    // log2(kFieldChunkEdge)
constexpr int kFieldChunkLocalMask = 31;   // kFieldChunkEdge - 1

// cell -> owning chunk coord, and the cell's index within that chunk
chunk.x = cell.x >> kFieldChunkShift;      // arithmetic shift
local.x = cell.x &  kFieldChunkLocalMask;
```

In C++23 (the engine's standard — `CMakeLists.txt:16`) both halves are
**defined**, not implementation-defined: `E1 >> E2` on a negative signed `E1`
yields `floor(E1 / 2^E2)`, and `&` reads the mandated two's-complement
representation. So shift/mask *is* floor-divide with a non-negative remainder,
for negative cells exactly as for positive ones. The boundary cases C2 pins:

| cell | chunk | local | that chunk owns cells |
|---|---|---|---|
| `-33` | `-2` | `31` | `[-64, -33]` |
| `-32` | `-1` | `0`  | `[-32, -1]` |
| `-1`  | `-1` | `31` | `[-32, -1]` |
| `0`   | `0`  | `0`  | `[0, 31]` |
| `31`  | `0`  | `31` | `[0, 31]` |

**Truncating division is the wrong answer, and it is the one a reader reaches
for.** `-1 / 32` is `0` and `-1 % 32` is `-1`, so cell `(-1, -1)` would claim
chunk `(0, 0)` at local index `(-1, -1)` — a negative index into a dense 32×32
array, and a disagreement with the shift form at **every** negative chunk
boundary. Two implementations that each map cells to chunks the "obvious" way
agree perfectly over the positive half-space and silently disagree over the
negative one, which is why C2 asserts the truncating form *differs* rather than
only asserting the shift form's results.

The voxel-side residency helper documents this same requirement —
`engine/prefabs/irreden/world/chunk_coord.hpp:38`, "for negative numerators we
need floor (toward -infinity) so a world voxel at -1 lands in chunk -1, not
chunk 0" — and reaches it with an explicit correcting divide. The rule is
identical; only the spelling differs. Both must agree, because a caller
straddling the two spaces (see "Relationship to residency chunks") converts
between them.


Per-chunk summary, maintained beside the cells:

| Field | Maintenance |
|---|---|
| `min_`, `max_` | refreshed by whole-chunk passes (a `setCell` cannot cheaply repair a `min`) |
| `nonZeroCount_` | incremental on every `setCell` |
| `dirty_` | set on any `setCell` that changes a value **and on any `clear()` of a live chunk**; cleared by `update()` |

Allocation discipline is `SpatialGrid`'s **allocation Pattern B**
(`spatial_grid.hpp:13-20`): buckets retain capacity across rebuilds, `clear()`
releases touched chunks without freeing their buffers (see below), and queries
write into **caller-owned** out-vectors. ("Pattern B" is overloaded in tree — the *API
shape* sense in `engine/prefabs/irreden/render/CLAUDE.md` is a different thing.
Always say *allocation* Pattern B and cite `spatial_grid.hpp:13`.)

**When the summaries are current.** `min_`/`max_` are refreshed by
`PlacementField::update()`, for every chunk in the dirty set, as part of the
same pass that recomputes clearance and labels — and nowhere else. They are
therefore authoritative immediately after `update()` and may be stale for any
chunk mutated since. D8's chunk-first pruning reads them, so querying a field
with a non-empty dirty set is a **precondition violation, not a stale-but-safe
read**: a stale `max_` prunes a chunk that now holds valid cells, and the query
returns too few hits with nothing to indicate why.

#### Presence vs. retained storage — what `clear()` means

D4 reads a cell outside any **present** chunk as *occupied*, so "present" is a
semantic state, not an allocation detail — while allocation Pattern B retains
buffers across rebuilds. Those two facts collide unless presence and allocation
are tracked separately. They are:

| | Meaning | Where it lives |
|---|---|---|
| **present** | this chunk's cells are authoritative; D4 reads them | membership in the `unordered_map` |
| **retained** | a dense 32×32 buffer kept for reuse, owned by no key | a free list of detached buffers |

- **`clear()` makes touched chunks logically ABSENT**, detaching their buffers
  to the free list rather than freeing them. The next chunk created at any key
  takes a buffer off that list, so capacity survives (Pattern B) while presence
  does not.
- **There is no present-but-stale chunk.** Presence *is* map membership, so
  lookup and iteration cannot observe a retained buffer — iteration walks the
  map, never the pool. That is the whole point of the split: a zero-filled chunk
  left logically present would read as *all free* under D4, the exact inverse of
  the absent rule, silently turning a conservative region permissive.
- **`nonZeroCount_ == 0` does not imply absence, and must never trigger
  eviction.** In the occupancy layer a present all-zero chunk means "every cell
  here is free" — a legitimate, load-bearing state and the exact *opposite* of
  absent, which D4 reads as all-occupied. Dropping it "to save memory" flips
  those cells to occupied. `ChunkedField2D` is generic storage and cannot know
  a layer's zero semantics, so the rule is stated at the storage level: nothing
  but an explicit `clear()` ever removes a chunk.
- **Clearing a live chunk marks it dirty.** Its key stays in the dirty set even
  though the chunk is gone — C3's recompute window must cover it, because
  removing a chunk raises its neighbours' clearance exactly as removing
  obstacles does (D4).

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

#### The numeric domain — because `std::int32_t` squared is not unbounded

"Exact, byte-identical" holds only inside a declared domain, and that domain has
to be enforced or the guarantee is void. `std::int32_t` tops out at
`2,147,483,647`, so `n²` is exact up to `n = 46,340` and **overflows at
46,341** — signed overflow, i.e. UB, not wraparound. An unbounded
`maxClearance` therefore makes the *saturation constant itself*
(`maxClearance²`) undefined, and an unbounded query radius does the same to the
`c*c` on the query side. All three parameters are bounded:

```cpp
constexpr int kMaxClearanceCells = 1024;   // domain ceiling, in cells
```

| Parameter | Domain | Enforced at |
|---|---|---|
| `maxClearance` (per field) | `[1, kMaxClearanceCells]` | `PlacementField` construction |
| `minSpacing` (D6) | `[1, kMaxClearanceCells]` | `queryPlacements` param validation |
| query radius `c` (D6) | `[0, maxClearance]` | `queryPlacements` param validation |

`1024`, not `46,340`: a ceiling set at the representational limit is a ceiling
in name only. `1024² = 1,048,576` leaves ~2048× headroom under `INT32_MAX`, and
D4's incremental window is a `2·maxClearance` ring — at 1024 that is already 64
chunks per side, well past the point where a full rebuild is the cheaper call.
The bound that keeps the arithmetic exact and the bound that keeps the window
sane are the same bound.

**`c > maxClearance` is rejected, not clamped.** Saturation means a stored
`maxClearance²` reads "at least the cap", never "exactly the cap", so the field
cannot answer a question posed beyond its own cap. Clamping would answer a
different question than the caller asked; letting it through returns `false` for
cells that genuinely have that clearance — a false negative indistinguishable
from "no space anywhere", which is the failure mode a caller cannot debug.
Rejecting turns it into a caught precondition.

**`minSpacing = 0` is rejected too, for a different reason.** Zero is not a
weaker spacing constraint — on an integer lattice it is not a constraint at all,
because two *distinct* cells are already `>= 1` cell apart. `minSpacing = 1` is
therefore the unconstrained draw, and `0` adds no expressive power over it. What
it does add is a degenerate D6 annulus: candidates are rejection-sampled from
`[minSpacing, 2*minSpacing]`, which at zero is the single offset `(0, 0)` — the
anchor itself. A distinct-sample implementation can never leave the anchor, so
the query returns at most one hit over an arbitrarily free field, and the caller
reads that shortfall as "no space anywhere" (D6 has no failure sentinel). Rather
than define a zero-spacing candidate path that would duplicate `minSpacing = 1`,
the domain excludes zero.

The asymmetry with `c` is deliberate: `clearance = 0` **is** valid, because
`c*c = 0` makes the clearance predicate `0 <= clearanceSq`, vacuously true for
every present cell — genuinely "no clearance requirement". Zero is a meaningful
relaxation for `c` and a degenerate spelling for `minSpacing`.

**Intermediates are `std::int64_t`; only the stored representation is int32.**
The domain bound above is necessary but *not* sufficient, because the F–H 1-D
pass's parabola intersection

```
s = ((f[q] + q²) − (f[v] + v²)) / (2q − 2v)
```

uses `q`, `v` as coordinates **along the window row**, bounded by the window's
cell extent — not by `kMaxClearanceCells`. A window row need only exceed 46,340
cells for `q²` alone to overflow int32, independent of the clearance cap. So the
pass computes in `int64`, seeds free cells with an `int64` sentinel (the classic
F–H bug is seeding `INT32_MAX` and then evaluating `f[q] + q²`), and narrows
only on write-back, where saturation guarantees the value is
`≤ maxClearance² ≤ 1,048,576`. `int64` covers any window a machine can allocate:
`q ≤ 2^31` gives `q² ≤ 2^62`.


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

**Ids are assigned in a canonical order**: ascending packed `FieldChunkKey`
(D3), then row-major local index within a chunk. Discovery order over the
chunk map would do just as well *on one machine* and differ on the next, since
that map is a `std::unordered_map` — see D7's "No hash-container iteration
order is ever observable".

> **Global region ids are NOT stable across `update()`.** They are epoch-scoped.
> A consumer that caches a label across an update and compares it to a fresh one
> is a bug. Compare labels only within one update epoch. Within an epoch the
> canonical order above makes them the same ids on every platform.

### D6 — placement draw

Bridson Poisson-disk sampling, all-integer:

- Candidates are integer cell offsets **rejection-sampled from the annulus**
  `[minSpacing, 2·minSpacing]` — no `sin`/`cos`, no libm anywhere in the draw
  path. `minSpacing >= 1` by D4's domain, so the annulus is never degenerate.
  How many words an attempt consumes, and in what order, is D7's to lock — the
  rejection loop is where an unspecified draw silently forks the stream.
- Background acceleration grid at `gridWidth(minSpacing)` — the `r/√2` cell that
  makes each grid cell hold at most one sample, computed **in integers**. The
  width is part of the contract, not an implementation detail; see "The
  background-grid width" below.
- Spacing checks in integer squared distance.
- **Seeded at the anchor cell** — which is a *sample* whether or not it is a
  *hit* (D7) — with an early-out at K. That early-out is where
  the near-anchor bias comes from — Bridson's active-list frontier grows
  outward, so stopping at K yields anchor-proximate results without a weight
  function. Weighted bias is a recorded future extension, **not built**.
- Per-candidate validity: the cell is present, `clearanceSq >= c*c`, and
  (optional flag) its region label equals the anchor's. A candidate that fails
  is discarded rather than kept as an obstacle — D7 states what that means for
  the frontier.
- Fewer than K reachable valid candidates ⇒ the query returns what it found. The
  caller reads `out.size()`; there is no failure sentinel.

> Bridson's frontier is only *approximately* radial. "Honours anchor bias" is
> therefore not an independently falsifiable property under a fixed seed — the
> test that asserts it is really re-asserting determinism. Do not let a future
> reader take it for a measured guarantee.

#### The background-grid width — integer-only, and pinned by value

The width must satisfy `w <= r / √2` (`r = minSpacing`): a grid cell of side `w`
has diagonal `w√2`, and `w√2 <= r` is exactly the condition under which two
samples sharing a cell would violate the spacing rule — i.e. the condition that
makes "at most one sample per grid cell" true. The contract locks the **largest**
such `w`, with no floating point anywhere:

```cpp
// largest w with w <= r / sqrt(2), i.e. with 2*w*w <= r*r
int gridWidth(int r) {                     // r == minSpacing, in [1, kMaxClearanceCells]
    const std::int64_t w =
        IRMath::isqrt(static_cast<std::int64_t>(r) * r / 2);
    return static_cast<int>(w < 1 ? 1 : w);
}
```

`IRMath::isqrt(x)` is the exact integer square root — the largest `n` with
`n*n <= x`, computed by bit-halving: no `std::sqrt`, no libm. It is a general
primitive, not a field kernel, so it lands in
`engine/math/include/irreden/ir_math.hpp` (C5). The identity `floor(r/√2) == isqrt(floor(r²/2))` holds
because for integer `w`, `w² <= r²/2` and `w² <= floor(r²/2)` are the same
statement; it was checked exhaustively over the whole domain `r ∈ [1, 1024]`
against a `floor(r/√2)` reference — **0 disagreements**.

**Why not the obvious `r * 0.7071f`.** A truncated decimal is not `1/√2`:
`floor(r * 0.7071)` disagrees with `floor(r/√2)` at exactly **five** points in
the domain — `r = 338, 577, 676, 915, 1014` — each one cell short (`238` vs
`239` at `r = 338`). The divergence belongs to the constant, not to the float
width: `float` and `double` agree with *each other* at every `r`, and
`r * (1/√2)` in *either* width agrees with the exact value at every `r`. So that
spelling admits two conforming readings — the written literal, and the `1/√2` it
is meant to stand for — which disagree at five in-domain values, in a draw D7
requires to be byte-identical across platforms. And the reading that is *right*
is the one this document's own cross-cutting rule forbids ("no `sqrt` and no
libm transcendental on any path in D4 or D6"). An exact integer form is the only
spelling that is both permitted and unambiguous.

A conservative rational (`(r * 7071) / 10000`) satisfies "integer-only" without
fixing this: measured over the same domain it reproduces **the same five
undershoots**, being the same truncated constant with the float removed.

**The `w < 1` clamp fires at exactly one input**, `r = 1`, where
`floor(1/√2) = 0` and a zero-width grid is meaningless. At `r = 1` the clamped
`w = 1` does *not* satisfy `2w² <= r²`, and it is safe there for a lattice
reason rather than the diagonal one: a grid cell of side 1 covers exactly one
lattice cell, so it holds at most one sample by construction. That is the only
input for which the diagonal argument is not the one doing the work — which is
why the clamp is stated as a contract case rather than left as defensive code.

### D7 — determinism

The draw takes an explicit `std::uint64_t seed` and uses a kit-local
`IRMath::Pcg32` (a new ~20-line header in `engine/math/`).

- **Never `IRMath::threadRng()`** — it is `thread_local`, so results would
  couple to which worker thread ran the query.
- **Never `std::uniform_*_distribution`** — the raw `mt19937` word stream is
  standard-specified, but the distributions are *not* implementation-portable.
  The kit maps raw PCG32 words to ranges itself.

Same seed + same field state ⇒ **byte-identical** results on every platform:
the same hits, in the same order. The C5 suite locks the PCG32 stream itself
against reference values, so a stream change is caught as a stream change rather
than as a mysterious placement diff.

#### Locking the stream is necessary and not sufficient

A locked word stream only makes the draw reproducible if *which* word goes
*where* is fixed too. Two implementations can agree on the stream to the word
and still return different hit lists, because Bridson's skeleton leaves three
choices that the algorithm's description does not make for you: how a raw word
becomes a bounded value, how many words a rejected candidate consumes, and which
active sample is extended next. Each is a fork in the candidate sequence, so
each is contract rather than implementation detail.

The complete word-consuming skeleton — **every** RNG call site in the draw
appears here, and each consumes **exactly one word**:

```cpp
constexpr int kPlacementAttempts = 30;   // Bridson's k — NOT PlacementParams::k_

// Bounded draw: one word in, a value in [0, n) out, no rejection at this layer.
std::uint32_t uniformBelow(IRMath::Pcg32 &rng, std::uint32_t n) {   // n >= 1
    return static_cast<std::uint32_t>(
        (static_cast<std::uint64_t>(rng.nextWord()) * n) >> 32);
}

// One attempt == two words, dx then dy. A rejected attempt consumes its two
// words and the loop draws the next two.
IRMath::ivec2 drawAnnulusOffset(IRMath::Pcg32 &rng, int r) {
    for (;;) {
        const int dx = static_cast<int>(uniformBelow(rng, 4 * r + 1)) - 2 * r;
        const int dy = static_cast<int>(uniformBelow(rng, 4 * r + 1)) - 2 * r;
        const std::int64_t d2 = std::int64_t{dx} * dx + std::int64_t{dy} * dy;
        const std::int64_t rr = std::int64_t{r} * r;
        if (d2 >= rr && d2 <= 4 * rr) { return {dx, dy}; }
    }
}

// active_ is a std::vector<ivec2> — an ordered container, never a set.
active_ = { params.anchor_ };                    // the anchor is a sample
gridInsert(params.anchor_);
if (isValid(params.anchor_)) { out.push_back(hitFor(params.anchor_)); }

while (!active_.empty() && static_cast<int>(out.size()) < params.k_) {
    const std::uint32_t i =
        uniformBelow(rng, static_cast<std::uint32_t>(active_.size()));
    const IRMath::ivec2 s = active_[i];
    bool extended = false;
    for (int attempt = 0; attempt < kPlacementAttempts; ++attempt) {
        const IRMath::ivec2 cand = s + drawAnnulusOffset(rng, params.minSpacing_);
        if (!spacingOk(cand) || !isValid(cand)) { continue; }
        gridInsert(cand);
        active_.push_back(cand);
        out.push_back(hitFor(cand));
        extended = true;
        break;                                   // first success ends the round
    }
    if (!extended) {
        active_[i] = active_.back();             // swap-and-pop — the resulting
        active_.pop_back();                      // list order is contract
    }
}
```

**It is measured, not merely specified.** Run against a reference PCG32 on a
64×64 fixture (`anchor = (32, 32)`, `K = 8`, `minSpacing = 4`, `c = 2`,
`seed = 12345`), the skeleton terminates, returns 8 hits with the anchor first,
places the closest pair *exactly* at `minSpacing²` — so the spacing constraint
binds rather than being incidentally satisfied — and reproduces on a rerun.
Either of the two spellings C5 requires to differ moves the list from the
**second** hit onward and moves `candidatesDrawn_` (15 as specified, 19 under
`word % n`, 18 under LIFO selection), which is what makes those must-differ
arms a property of this skeleton rather than an assumption about it.

What each line pins, and why that spelling:

- **`uniformBelow` is multiply-shift, and it never rejects.** One word per
  bounded value, unconditionally, so the stream position after a call is a pure
  function of the *number* of calls — which is what removes "how is a rejected
  word handled" as a question at this layer entirely. The map is not exactly
  uniform: source-word counts per output differ by at most one, a relative bias
  of `n / 2^32`, which at the largest `n` the kit ever passes
  (`4r + 1 = 4097`, at `r = kMaxClearanceCells`) is under `1e-6`. A debiasing
  rejection loop would buy that back at the cost of a data-dependent word count;
  the fixed count is worth more here than the last `1e-6` of uniformity. `%` is
  **not** an accepted substitute: it is equally portable and equally
  deterministic, but it is a *different map*, so it yields a different candidate
  sequence from the same stream.
- **The annulus is rejection-sampled, two words per attempt, `dx` then `dy`.**
  This is the draw's only rejection loop, and it is unbounded by design:
  acceptance is `>= 0.48` at every `r` in `[1, kMaxClearanceCells]` — worst
  case at `r = 1`, where 12 of the 25 offsets in `[-2, 2]²` land in the annulus
  — rising to `3π/16 ≈ 0.589` as `r` grows, so the expected cost is under 2.1
  attempts and the tail is geometric. (Counted exactly over the whole domain,
  cross-checked against brute force at `r ∈ {1, 2, 3, 7, 16, 33}`.)
- **The next sample to extend is drawn, not popped.** Uniform over the active
  list — Bridson's own rule, one word. A stack or a queue would be cheaper and
  just as deterministic, but each grows a visibly different frontier, and the
  "Bridson" in the References is the random-selection algorithm.
- **Exhausted samples leave by swap-and-pop.** That reorders the active list, so
  the *choice* of removal is observable through every later
  `uniformBelow(active_.size())` draw. Erase-and-shift is equally deterministic
  and gives a different sequence; swap-and-pop is the locked one.
- **`kPlacementAttempts` is 30, and it is contract.** It is Bridson's `k`, and
  it is unrelated to `PlacementParams::k_` (hits wanted) despite the name the
  literature uses. Changing it moves both the hit list and the point at which a
  sample is abandoned.
- **Only accepted candidates become samples.** A candidate failing `spacingOk`
  or D6's validity predicate is discarded outright — it enters neither the
  background grid nor the active list — so the frontier spreads only through
  cells the query would accept. The anchor is the single exception: it seeds the
  active list and the grid **unconditionally**, because anchoring on an occupied
  cell ("place near this building") is the ordinary case, and a geometric seed
  that is not itself a hit is what makes it work. The anchor reaches `out` only
  if it passes the same validity predicate — which is why D4 describes
  `minSpacing = 0`'s degenerate annulus as returning "at most one hit" rather
  than exactly one.
- **`out` is in acceptance order**, anchor first when the anchor is a hit.
  "Byte-identical results" means the same vector, not the same set.

#### No hash-container iteration order is ever observable

`ChunkedField2D`'s chunk map is a `std::unordered_map` and its dirty set is a
set of the same keys (D2). Hash-container iteration order is
implementation-defined and does differ between libstdc++, libc++ and MSVC, so
anything downstream of it varies by platform *by construction* — the exact
failure this section exists to prevent, and the one no amount of seed discipline
catches.

The draw is safe here by construction: it is anchored and reaches chunks by
**keyed lookup only**, never by enumeration, so no chunk order can reach its
word stream. The wholesale passes are where the rule has to be applied:

- **Region-label numbering (D5) is order-dependent and must not be.** Labels
  assigned in discovery order over an unordered map get different *numbers* on
  different standard libraries for the same field. Assign them in **ascending
  packed `FieldChunkKey`** (D3) and, within a chunk, row-major local index.
  Placement itself would survive a different numbering — `sameRegionAsAnchor_`
  only compares labels for equality — but a C4 test that pins label values
  would not, nor would any comparison of one epoch's ids across two machines.
  (D5 already forbids caching an id *across* an `update()`; this is the
  orthogonal axis, one epoch across two standard libraries.)
- **Any enumeration added later** — a debug dump, a serializer, a whole-field
  statistic — uses that same canonical order. `PlacementQueryStats`' fields are
  counts rather than sequences, so they are already order-free.

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

struct PlacementParams {
    IRMath::ivec2 anchor_{};
    int           k_          = 0;   // hits wanted; early-out at K (D6)
    int           minSpacing_ = 1;   // cells, [1, cap]; 1 == unconstrained (D4)
    int           clearance_  = 0;   // "c", cells, [0, field.maxClearance()]
    bool          sameRegionAsAnchor_ = false;
    std::uint64_t seed_       = 0;   // D7
};

class PlacementField {                       // owns the three layers
    ChunkedField2D<std::uint8_t> occupancy_;
    ChunkedField2D<std::int32_t> clearanceSq_;   // saturated at maxClearance²
    /* region labels */
    int maxClearance_ = 0;                       // [1, kMaxClearanceCells]
  public:
    explicit PlacementField(int maxClearance);   // rejects out-of-domain (D4)
    int  maxClearance() const { return maxClearance_; }
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
- **Domain validation is a precondition check, not a silent clamp** —
  `queryPlacements` rejects a `PlacementParams` outside D4's domain table
  rather than clamping into it, for the reason D4 gives: a clamped query
  answers a question the caller did not ask, and its false negatives are
  indistinguishable from "no space anywhere".
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
| `engine/math/` | field-layout-agnostic kernels: `Pcg32`, `isqrt` (exact integer square root, D6), the 1-D squared-EDT pass over a `std::span`. Header-only, gtest-covered per kernel. |
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
| **C5** (#3163) | `IRMath::Pcg32` + `IRMath::isqrt` + `field_placement.hpp` — draw, `PlacementField`, `queryPlacements` + stats (D6, D7, D8); flips this table to shipped | not started |

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
  mutated chunks; bucket capacity survives `clear()` (allocation Pattern B);
  **negative-cell mapping** — cells `-33 / -32 / -1 / 0 / 31` map to D2's
  chunk+local table, a `setCell` at `(-1, -1)` is read back through chunk
  `(-1, -1)` local `(31, 31)`, and a truncating-division reference mapping is
  asserted to **differ** on the negative arm (without that arm the test passes
  on the positive half-space alone, which is exactly how the two spellings
  agree for the wrong reason); **presence/`clear()` both halves** — after
  `clear()` no touched key is present (lookup and iteration both observe
  absence, so D4 reads those cells as occupied) *and* a refill reuses the
  retained buffers with no new allocation (a capacity-only assertion passes
  while stale zero-filled chunks stay logically present, which is the bug);
  a present all-zero chunk survives `update()` un-evicted and still reads
  *free*, distinguishing it from absent; **dirty lifecycle** — a no-op
  `setCell` does not dirty, a value-changing one does, `clear()`ing a live
  chunk reports that key as dirty, `update()` clears the set, and a later
  mutation re-dirties it.
- **C3** — an occupied cell in a **neighbouring** chunk within radius r shrinks
  clearance at this chunk's edge (asserted as a strict decrease against an
  empty-neighbour control, not merely as a bound); an **absent** neighbour chunk
  clamps edge clearance exactly as an occupied one does (the conservatism rule);
  `clearanceSq` saturates *equal to* `maxClearance²` on an empty field;
  incremental recompute **byte-equals** a from-scratch rebuild over seeded
  mutation sequences; **numeric domain (D4)** — a `PlacementField` at
  `maxClearance = kMaxClearanceCells` saturates at exactly `1,048,576` with no
  overflow, construction accepts `1` and `kMaxClearanceCells` and rejects `0`
  and `kMaxClearanceCells + 1` (both arms, so the check cannot pass by
  rejecting everything), and the 1-D pass is run over a window row long enough
  that an int32 intermediate would overflow (> 46,340 cells) and asserted equal
  to an `int64` reference.
- **C4** — an L-shaped free region spanning ≥3 chunks gets one label; a wall
  splitting it yields two, with the wall's chunks re-stitched correctly;
  incremental relabel ≡ full relabel over seeded mutations; **the ids
  themselves are pinned by value** on a fixed multi-chunk fixture, against a
  literal reference — an equality-only assertion passes under any numbering, so
  it cannot see a discovery-order labelling that renumbers on the next standard
  library (D5, D7).
- **C5** — the PCG32 stream is locked against reference values; same seed ⇒
  byte-identical hit lists across two independent field rebuilds; all pairwise
  hit distances ≥ `minSpacing` (integer squared check); every hit satisfies
  `clearanceSq >= c*c`; the anchor-region flag yields zero hits outside the
  anchor's region on a two-region fixture; a K-shortfall fixture returns exactly
  the valid count; **pruning fires** — `PlacementQueryStats` reports
  `chunksPruned_ > 0` and `chunksConsidered_ <` the resident chunk total on a
  mostly-low-clearance fixture; **out-of-domain params are rejected** at each
  boundary — `minSpacing = 0`, `minSpacing = kMaxClearanceCells + 1` and
  `c = maxClearance + 1` rejected, the adjacent in-domain values
  `minSpacing = 1`, `kMaxClearanceCells` and `maxClearance` accepted (both arms,
  so the check cannot pass by rejecting everything); **the background-grid width
  is pinned by value** (D6) — `gridWidth(r)` asserted equal to an independent
  `floor(r / sqrt(2))` reference for every `r` in `[1, kMaxClearanceCells]` (the
  reference may use `double` — `test/**` is outside the no-libm rule, the kit is
  not), `r = 1` asserted to clamp to `1`, and `r` in
  `{338, 577, 676, 915, 1014}` — the inputs where a truncated `0.7071` constant
  is one cell short — asserted to give `{239, 408, 478, 647, 717}`, so any
  truncated-constant spelling fails rather than passing quietly;
  **the draw order is pinned against a committed reference** (D7) — over a
  fixture built by deterministic construction (no RNG in the fixture itself)
  under a fixed seed and fixed params, the **complete** `out` — every
  `cell_`/`chunk_` pair, *in order* — is asserted against a literal reference
  list committed in the test, and `PlacementQueryStats::candidatesDrawn_`
  against a reference count, so a divergent retry or rejection policy fails even
  when it happens to land the same hits; two rebuilds of the same binary cannot
  observe a platform-varying order, and a committed literal is the only form
  that crosses hosts. Two **must-differ** arms keep that reference pinned to the
  *specified* order rather than to whatever the implementation reached for: a
  `word % n` range map and a LIFO active-list selection are each asserted to
  produce a **different** hit list from the same seed (without them the
  reference is self-consistent with any single implementation, which is how a
  fork in the candidate sequence passes as a locked one);
  and one end-to-end fixture builds
  occupancy → `update()` → query and gets K chunk-qualified hits honouring
  clearance, spacing, region and anchor under a fixed seed.

Cross-cutting, for any reviewer of C2–C5:

- No `glm::*` and no `std::` math outside `engine/math/` (`.claude/rules/cpp-math.md`);
  kit code in prefabs goes through `IRMath::`. `test/**` is outside that rule's
  scope.
- No `sqrt` and no libm transcendental on any path in D4 or D6. `IRMath::isqrt`
  is the sanctioned integer form where a square root is unavoidable (D6).
- Every squared-distance intermediate is `std::int64_t`; `std::int32_t` appears
  only as the stored representation, where saturation bounds it (D4).
- No `std::uniform_*_distribution` anywhere (D7).
- Every RNG call site in the draw is one of D7's three — `uniformBelow` for a
  bounded value, the two-word annulus attempt, the active-list index. A fourth
  call site, or a `%` where `uniformBelow` is specified, is a contract change.
- Nothing observable is derived from `unordered_map` iteration order (D7);
  whole-field passes enumerate in ascending packed `FieldChunkKey`, then
  row-major local index.

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
