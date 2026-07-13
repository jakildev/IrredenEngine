# Encapsulate `C_VoxelSetNew` derived-state maintenance (kill the raw-span-carve footgun)

## Context

PR #2146 was the **fourth** time the same one-line fix landed: a creation
carves voxels through the raw `voxels_` span, calls `syncActiveMask()`, but
forgets `IRPrefab::Voxel::recomputeFaceOccupancy()` — leaving the exposed-face
mask stale so carved surfaces render black/missing under the lit path. The
trail: review nit on #2016 → issue #2018 → PR #2117 (`shape_debug`) → issue
#2119 → PR #2146 (`z_yaw_rotation`, `ir_voxel_yaw`). The convention is already
documented in `engine/prefabs/irreden/voxel/CLAUDE.md` and there is even a
`simplify-check-ecs` heuristic for it — yet it keeps recurring.

This is a **missing-abstraction footgun**, exactly as the user diagnosed: a
dependency of the voxel subsystem is pushed onto every creation that uses it
as a manual, opt-in, easy-to-drop bookkeeping step, instead of being owned by
the subsystem.

Investigation surfaced a sharper fact. The bulk mutators on `C_VoxelSetNew`
(`deactivateAll`, `activateAll`, `fillPlane`, `reshape`,
`changeVoxelColor*` — `component_voxel_set.hpp:311-438`) each maintain **three**
invariants on every edit:
1. `mirrorToRotationSource(i)` — the GRID-spin authored-truth snapshot
   (`component_voxel_set.hpp:305`),
2. the pool **active mask** (`VoxelPool::resyncRangeFromColors` /
   `markRange*`),
3. **face occupancy** (`IRPrefab::Voxel::recomputeFaceOccupancy`,
   `voxel/face_occupancy.hpp:52`).

The documented manual carve recipe only mentions **two** of these — it omits
the rotation-source mirror entirely. So every hand-rolled carve site is not
only at risk of the black-face bug, it also silently skips the mirror (works
today only because carves happen pre-rotation; fragile). The only sane place
to keep all three in lockstep is an **encapsulating mutator on the component**.

This route is the engine's own rule, not a new invention: `cpp-ecs.md`
§"No dirty flags" mandates *"push at mutation time — each mutating method
writes the affected derived state directly."* A system-tick recompute is the
*wrong* tool here (forbidden dirty/changed flag + wasted O(volume) every frame
on static carved sets). The pre/post-tick + pipeline-ordering route stays
correct only for state that genuinely changes each frame (rotation), which
`REBUILD_GRID_VOXELS` / `REBUILD_DETACHED_VOXELS` already own.

**Outcome:** add the encapsulating edit API, migrate every creation carve site
onto it, retire the manual recipe to "internal mechanism," and codify the
general convention so the next subsystem-dependency footgun is caught in review.

## Decisions (confirmed with user)

- **API shape:** `editVoxels(fn)` + `carve(predicate)` + `resyncAfterRawEdits()`.
- **Delivery:** one cohesive engine PR (API + full creations sweep + docs + smell-check upgrade).
- **Generalization:** fix the voxel case concretely **and** codify the general
  rule in `cpp-ecs.md` + the smell checklist. No broad cross-system audit this round.

---

## Part 1 — Add the encapsulating edit API

**File:** `engine/prefabs/irreden/voxel/components/component_voxel_set.hpp`
(add as public members near the existing mutators, ~after `reshape`, before
`syncActiveMask` at line 498). No new includes — the existing mutators already
pull in `face_occupancy.hpp` and `voxel_pool_api.hpp`.

Three methods, all of which restore the three invariants **in one place, in
order**: per-voxel `mirrorToRotationSource(i)` → active-mask resync
(`VoxelPool::resyncRangeFromColors`) → `IRPrefab::Voxel::recomputeFaceOccupancy(voxels_, size_)`.
Factor the shared tail into a private `resyncDerivedState()` helper that
`resyncAfterRawEdits()` and the two templates all call, so the order lives in
exactly one spot.

1. **`template <typename Fn> void editVoxels(Fn &&fn)`** — primary API. Loops
   `i` over `numVoxels_`, calls `fn(i, voxels_[i], positions_[i].pos_)` so the
   callback can read the local position (needed by SDF/surface/bone carves),
   mutate color/alpha/flags/bone, then runs `resyncDerivedState()` once.

2. **`template <typename Fn> void carve(Fn &&shouldDeactivate)`** — sugar for
   the dominant case: `editVoxels([&](int, C_Voxel &v, vec3 p){ if (shouldDeactivate(p)) v.deactivate(); })`.
   Reads at the call site as `vs.carve([&](vec3 p){ return sdf(p) > 0.f; });`.

3. **`void resyncAfterRawEdits()`** — escape hatch (just `resyncDerivedState()`,
   public). For multi-pass edits that can't be expressed as one per-voxel
   callback. Replaces the documented two-call recipe with a single
   un-droppable call that *also* fixes the missing mirror.

Mirror the existing helpers' comment density; one short comment block on
`editVoxels` explaining it is the supported custom-carve path and that callers
must **not** hand-roll recompute/sync. Leave `syncActiveMask()` as the
low-level pool primitive (still used internally) but update its comment to
point authors at `editVoxels`/`carve`.

## Part 2 — Migrate every raw-span carve site (creations/ only)

Mechanical, behavior-preserving except where it fixes a real bug. Replace each
`for (...) voxels_[i].deactivate(); … recomputeFaceOccupancy(); … syncActiveMask();`
block with a `carve(...)` / `editVoxels(...)` call. **Do not touch the private
game worktree** under `creations/game/.claude/worktrees/**` (cross-repo
isolation; it carries its own copy of the recipe).

Pattern (representative paths; ~12 sites across 7 files):
- `creations/demos/skeletal_demo/main.cpp:~390-405` — **fixes the one live
  black-face bug** (carve + bone assignment via `editVoxels`; currently missing
  `recomputeFaceOccupancy`).
- `creations/demos/canvas_stress/main.cpp:~592-607` and `~680-690` — collapses
  the two present-but-sync-then-recompute blocks into `carve`/`editVoxels`
  (normalizes the order; both calls already present so no behavior change).
- `creations/demos/shape_debug/main.cpp:~883-911`, `~960-977` — already
  correct; migrate for consistency.
- `creations/demos/lighting/common/lighting_demo_scene.hpp:~160-178` — correct; migrate.
- `creations/editors/voxel_editor/main.cpp:~438,470,530,1750` (4 sites) — correct; migrate.
- `creations/demos/z_yaw_rotation/common.hpp` (`carveSphere`),
  `creations/demos/ir_voxel_yaw/main.cpp` (`createHollowShell`) — fixed on
  master via #2146; migrate to `carve` so the recipe disappears from creations.

> Note: this worktree branch predates #2146, so locally `carveSphere` /
> `createHollowShell` still show the bug — confirm the branch this work is
> based on includes #2146 (cut a fresh branch off current `origin/master`) so
> the migration doesn't reintroduce or double-resolve it.

After the sweep, `grep -rn 'recomputeFaceOccupancy\|syncActiveMask' creations/`
(excluding the game worktree) should return **zero** hand-rolled sequences —
the only remaining callers of those primitives are the engine mutators.

## Part 3 — Docs + convention (codify the general principle)

1. **`engine/prefabs/irreden/voxel/CLAUDE.md` (lines ~27-47):** rewrite the
   "raw `voxels_[i]` span write" recipe to say: custom carves/edits go through
   `vs.editVoxels(...)` / `vs.carve(...)`; the `recomputeFaceOccupancy` +
   `syncActiveMask` + `mirrorToRotationSource` triple is the *internal
   mechanism*, no longer a caller responsibility.

2. **`.claude/rules/cpp-ecs.md`:** add a section — **"System-owned invariants:
   encapsulate, don't delegate to callers."** State the principle the user
   articulated: when using a subsystem requires follow-up bookkeeping to keep
   derived state consistent, that bookkeeping must be owned by the subsystem —
   exposed either through (a) a **component mutator API** that performs the
   resync at mutation time (the rule-endorsed "push at mutation" pattern), or
   (b) the system's **`beginTick`/`endTick` + pipeline ordering** when the
   state genuinely changes every frame — **never** left as a manual step each
   creation must replicate. Use the voxel mask triple as the worked example;
   cite `REBUILD_GRID_VOXELS` as the hooks-route counterpart and note the
   "no dirty flags" constraint that decides which route applies.

3. **`.claude/rules/cpp-ecs-smells.md`:** upgrade the existing heuristic. Today
   it flags *missing* `recomputeFaceOccupancy`. Change it to flag **any**
   hand-rolled `voxels_[i].activate()/.deactivate()` carve loop in
   `creations/**` or `editors/**` followed by `syncActiveMask()` /
   `recomputeFaceOccupancy()` → "use `vs.editVoxels()` / `vs.carve()`." That
   closes the loop so the API is adopted, not merely available.
   (The `.claude/agents/simplify-check-ecs.md` agent references this checklist;
   no separate edit needed beyond keeping the wording in sync.)

---

## Verification

- **Build:** `fleet-build` the touched targets — `IRShapeDebug`,
  `IRZYawStatic`/`IRZYawInteractive`, `IRVoxelYaw`, `IRCanvasStress`,
  `IRSkeletalDemo`, the voxel editor, and the lighting demo. The header change
  is engine-wide, so a broad build catches template/signature breakage.
- **The real bug fix:** `fleet-run IRSkeletalDemo --auto-screenshot` — carved
  regions render solid surfaces, not black. (This is the one site that changes
  behavior.)
- **No-regression on already-correct sites:** run `render-verify` (or
  `IRShapeDebug --auto-screenshot` + the committed references) — the migration
  must be byte-identical for `shape_debug`, `lighting`, `voxel_editor`,
  `canvas_stress`, `z_yaw_rotation`, `ir_voxel_yaw`. Use the `attach-screenshots`
  skill for the PR body since this touches render output.
- **Footgun gone:** `grep -rn 'recomputeFaceOccupancy\|syncActiveMask' creations/`
  (minus the game worktree) returns no hand-rolled pairs.
- **Convention lands:** confirm `simplify` flags a deliberately re-introduced
  raw carve loop in a scratch creation file and steers to `editVoxels`.

## Out of scope / follow-ups

- Cross-system audit for other "caller must finalize after using system X"
  instances (user deferred — file as `fleet:coding-improvement` later).
- The private game repo's copy of the recipe (`creations/game/...worktrees`) —
  fix in that repo under its own conventions; do not edit from here.
- No `editVoxels` overload that also rewrites `positionOffsets_`/`globalPositions_`
  unless a migrated site needs it; add only if the sweep surfaces such a case.

## Steward ledger

reconciled-through: 2026-07-13 (EPIC CLOSED)
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2165 | merged (P1) | #2170 | plan | 2026-07-13 |
| #2166 | merged (P2) | #2173 | plan | 2026-07-13 |
| #2167 | merged (P3) | #2309 | plan | 2026-07-13 |

### Decisions
- D1 (2026-06-30): API shape = editVoxels(fn) + carve(predicate) + resyncAfterRawEdits(); one cohesive delivery split into 3 phased children; codify convention (no broad cross-system audit this round) — source: umbrella plan §Decisions (confirmed with user)

### Events
- 2026-06-30: filed via file-epic
- 2026-07-13 (steward close-out): all 3 children merged — P1 #2165 (PR
  #2170, 07-01), P2 #2166 (PR #2173, 07-02), P3 #2167 (PR #2309, 07-09).
  Closing criteria verified: (1) 3/3 children closed; (2) grep
  `recomputeFaceOccupancy|syncActiveMask` in `creations/` (excl game) →
  only a doc comment in `skeletal_demo/main.cpp` remains, no hand-rolled
  pairs; (3) render-verify byte-identity + `skeletal_demo` solid carve
  verified in the P1/P2 PR reviews. Convention codified
  (`cpp-ecs.md` §System-owned invariants); smell check upgraded
  (`simplify-check-ecs.md` #9). Deferred cross-system caller-finalize
  audit filed as `fleet:coding-improvement` #2364. Umbrella closed.
