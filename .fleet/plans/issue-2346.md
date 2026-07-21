## Plan: render: cardinal (single-canvas) color/AO tap is run-to-run non-deterministic once voxel positions are perturbed

- **Issue:** #2346
- **Model:** fable — render stage design across both backends on the hottest kernel pair in the engine, with a hard byte-identity contract on the default path. Same class of work as #2255/PR #2272, whose design this extends.
- **Date:** 2026-07-20

### Verified current state + confirmed repro

Source-traced on current master (this planning pass):

- **The racing site is the cardinal color/entity-id tap.** `writeColorTap`
  (`c_voxel_to_trixel_stage_2.glsl:145-158`) is the same non-atomic
  read-compare-write #2255 diagnosed: every face whose encoded key equals the
  settled canvas distance passes the `==` re-test and `imageStore`s —
  last-writer-wins. ALL cardinal tap sites route through it via
  `emitDeformedFace` (NONE-mode branch :398-420, subdivided branch :422-454,
  and the #2157 dual-emit :461-473), while the per-axis sites use the
  #2255-guarded `writeColorTapPerAxis`. The distance plane itself is
  `imageAtomicMin` — order-independent, which is why only color/entity-id
  (and everything composited from them) drifts.
- **Why the documented exemption fails.** The stage-2 comment (:169-171)
  keeps the cardinal path unguarded because "their integer iso key is a
  bijection of the cell, no tie is possible". The bijection
  (iso-pixel, depth) ↔ (x,y,z) is real — but it is a bijection of *cells*,
  not *voxels*: the store keys on `roundHalfUp(voxelPosition)` (:402; the
  subdivided branch on `roundHalfUp(aligned * subdivisions)` :427), and two
  independently displaced voxels can round into the SAME (micro-)cell. Same
  cell ⇒ identical `(iso pixel, encodeDepthWithFace key)` ⇒ both pass the
  re-test ⇒ race. Ties occur **iff two live voxels share a rounded cell**
  (distinct cells always produce distinct keys at a shared pixel, by the
  bijection).
- **This explains the issue's `roundVec3HalfUp` experiment** (quantizing the
  baked offsets did NOT remove drift): quantizing to the lattice keeps the
  collisions — two voxels displaced onto the same integer cell — so the race
  persists. That result is consistent with the cell-collision mechanism, not
  evidence against it.
- **No in-key tiebreak is possible.** Cardinal encode is
  `[31:3] depth | [2] flip | [1:0] slot` (`ir_iso_common.glsl:144-163`) — no
  spare bits for a voxel ordinal (needs 20+), and #2255's step-0 already
  established no 64-bit atomics on either backend. A two-pass winner
  election is the only sound shape, same conclusion as #2255.
- **Repro** = the issue's own measured matrix (macOS/Metal, drift scaling
  with `--wave-amplitude`, cardinal-yaw shots only, amplitude-0 byte-identical,
  rotated shots stable). The race class is scheduling-dependent and
  backend-agnostic (#2255's twin fired on both backends).

**Phase 0 — measure the premise before building.** The mechanism premise is
"drifting amplitudes produce rounded-cell collisions". Cheap CPU probe: in
IRPerfGrid's wave-freeze bake (or a 20-line standalone replica of the bake
formula), count duplicate `IRMath::roundHalfUp(pos)` cells across the scene at
`--wave-amplitude 0 / 1 / 5`. Expected: 0 at amplitude 0; > 0 at 1; more at 5
(tracking the drift table in the issue). If refuted (0 collisions at a
drifting amplitude), the mechanism is NOT the tie-race — stop, comment the
measurement on the issue, and swap back to `fleet:needs-plan` for re-plan; do
not build the dependent phases.

### Scope

Deterministic single-writer election for cardinal-store ties, extending
#2272's winner-election design to the single-canvas store — with **zero added
cost and byte-for-byte identical shader binaries on lattice-clean scenes**
(the default path for every existing creation). Covers every canvas ticking
the single-canvas store (world/GRID, detached, GUI) via a per-pool signal.
The rotating/per-axis path is untouched (#2255/#2429 own it). Downstream
AO/lighting/picking determinism follows from the color + entity-id planes.

### Approach

0. Phase-0 collision-count probe (above).
1. **Per-pool tie-possibility signal.** `C_VoxelPool` gains a
   `storeTiesPossible_` flag, recomputed on frames where positions changed
   (the stage-1 upload path already walks positions per frame — see the
   dirty-range position upload in `system_voxel_to_trixel.hpp:174-192` — so
   this adds one comparable-cost walk on dirty frames only). Criterion,
   early-exit TRUE:
   - any component `|pos - round(pos)| > 1e-4` (mirror
     `snapNearIntegerVoxelPosition`'s epsilon, `ir_iso_common.glsl:450-454`), OR
   - if the fract scan finds nothing: any two live voxels share a
     `roundHalfUp` integer cell (hash-set scan, early-exit on first
     duplicate) — catches the all-integer collision case.
   This pair is exactly sufficient for BOTH cardinal branches: subdivided
   micro-collisions between all-integer positions are impossible
   (`pos * sub` stays distinct for distinct integers), and any
   fractional-position micro-collision is caught by the fract half.
   Static scenes scan only when dirty (wave-freeze: once at bake).
2. **Compile-time shader variants** — the `IR_FEEDER_PASS` a′ pattern
   (`c_voxel_to_trixel_stage_1.glsl` wrapper: default binaries byte-for-byte
   identical, no runtime predication tax on the hot kernels; preferred here
   over #2255's runtime `resolveMode` because the cardinal store runs every
   frame of every scene). `FrameDataVoxelToCanvas` is untouched.
   - Split `c_voxel_to_trixel_stage_2.glsl` into `_body` + a thin default
     wrapper (mirror stage-1's include idiom; Metal twin likewise). The
     default wrapper must preprocess to today's exact source.
   - New `c_voxel_to_trixel_stage_1_winner_resolve.{glsl,metal}` wrapper:
     `IR_STORE_WINNER_ELECTION 1` compiles the stage-1 body so every
     cardinal-branch `writeDistanceTap(p, key)` site (block taps :353-358
     incl. the #1557 dilation ±su/±sv, the store site :831, and the #2157
     dual-emit :906) becomes an election tap:
     `if (imageLoad(distances, p).x == key) atomicMin(winner[cell(p)], voxelIndex)`
     (shape of the existing per-axis `winnerResolveTap` :217-229). The
     election footprint therefore equals the stage-2 tap footprint by the
     existing stage-1↔stage-2 mirroring invariant.
   - New `c_voxel_to_trixel_stage_2_winner.{glsl,metal}` wrapper:
     `IR_STORE_WINNER_ELECTION 1` threads `voxelIndex`
     (`= compactedVoxelIndices[compactedIdx]`, already read at :236) through
     `emitDeformedFace` and guards all cardinal `writeColorTap` sites with
     `winner[cell] == voxelIndex` (shape of `writeColorTapPerAxis` :172-184).
3. **Winner buffer.** A system-owned, grow-only GPU buffer sized
   `canvasW × canvasH × 4B` for the ticking canvas, allocated lazily on the
   first flagged canvas (lattice scenes allocate nothing). Transiently
   `bindBase` at `kBufferIndex_PerAxisResolveScratch` (28) around the
   election + stage-2 dispatches, restored after (existing restore pattern,
   `system_voxel_to_trixel.hpp:1780-1793`; the bind-point budget is full —
   transient reuse only). Do NOT reuse `axes.winnerIds_`: it is
   rotation-lifecycle (freed at cardinal yaw — the #2412 class) and sized to
   the per-axis canvas, not this one.
4. **Dispatch wiring** (the `!skipSingleCanvasVoxels` block,
   `system_voxel_to_trixel.hpp:1519-1613`), only when the ticking canvas's
   pool flag is set: after the stage-1 visible+feeder dispatches and their
   image barrier — `fillBuffer(winner, 0xFF)` → bind → dispatch the
   winner-resolve program over indirect struct 0 ONLY → SHADER_STORAGE
   barrier → dispatch the stage-2 WINNER variant in place of
   `stage2Program_`. Struct-0-only is deliberate symmetry: stage 2 itself
   dispatches only struct 0, and feeder-won pixels are never color-tapped
   (#1740's margin guarantees no on-screen pixel is a feeder) — no feeder
   election dispatch. Flag clear → exactly today's programs and dispatch
   count.
5. **Metal registration.** Both new kernels added to
   `threadgroupSizeForFunctionName` and (matching stage-2's distance-read
   config) `functionUsesImageAtomicScratch` in `metal_pipeline.cpp`. Reading
   the canvas's OWN atomic-written distances downstream in the same tick is
   the sanctioned #1640 pattern (stage 2 does it today); bind distances
   READ_ONLY on the election dispatch (Metal re-binds per dispatch).
6. `shader_names.hpp` entries + program creation beside
   `stage1FeederProgram_`.

### Affected files

- `engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl` — election tap under `#if IR_STORE_WINNER_ELECTION` at the cardinal store sites (default compile textually unchanged).
- `engine/render/src/shaders/c_voxel_to_trixel_stage_1_winner_resolve.glsl` — new thin wrapper.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2.glsl` — becomes a thin default wrapper; body moves to `c_voxel_to_trixel_stage_2_body.glsl` (new) with the guarded-tap variant under `#if`.
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2_winner.glsl` — new thin wrapper.
- `engine/render/src/shaders/metal/…` — Metal twins of all of the above, line-for-line.
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — pool-flag recompute on dirty uploads, winner-buffer alloc/bind/fill/restore, election dispatch + barrier, variant program selection.
- `engine/render/…/component C_VoxelPool` header — `storeTiesPossible_` + maintenance hooks.
- `engine/render/include/irreden/render/shader_names.hpp` — new shader paths.
- `engine/render/src/metal/metal_pipeline.cpp` — kernel registration (threadgroup map + image-atomic-scratch list).

### Acceptance criteria

- **Phase-0 probe (positive-fire premise):** collision count 0 at amplitude 0, > 0 at amplitudes 1 and 5, monotone with amplitude.
- **Issue repro fixed:** `fleet-run IRPerfGrid --mode voxel_set --wave-freeze --no-overlay --grid-size 32 --zoom 0.8 --auto-screenshot 80` run ≥10×, at `--wave-amplitude 1` AND `5`: byte-identical `fit_grid`, `zoom1_origin`, `profiler_overlay`, `zoom4_pan` (`shasum`/`img_diff`). State the observed PRE-fix drift on the authoring host in the PR (positive fire); if the authoring host's GPU happens not to exhibit the pre-fix race, the phase-0 collision count plus a temporary tie-population probe (count cells where the election saw ≥2 distinct candidates) stand in as the enabled-path positive fire.
- **Default path byte-identical:** flagless `IRPerfGrid` dense/hollow runs and the `IRShapeDebug` standard suite byte-identical to pre-change master (A/B `img_diff`) — expected by construction (identical binaries, no extra dispatch), verified empirically anyway (three prior incidents of Metal tie-winner reshuffle from "no-op" changes).
- **#2255 unregressed:** its acceptance re-run — ≥10× byte-identical `zoom1_rot`/`zoom4_rot`; `jitter_probe` sweep stays `SMOOTH`.
- Both backends build + smoke clean; `render-verify` + `attach-screenshots` per render-PR protocol; runs end `ir-run: RESULT=CLEAN`.

### Gotchas

- **Winner id must be run-stable:** use the VALUE `compactedVoxelIndices[compactedIdx]` (source pool index), never `compactedIdx` itself — the compact appends via atomics, so list ORDER varies run-to-run (#2255's gotcha, verbatim).
- **Election footprint must exactly mirror the stage-2 tap set** (dilation ±su/±sv, dual-emit opposite face, both cardinal branches). A missed site leaves `winner = 0xFFFFFFFF` at a tapped pixel and the guard rejects ALL writers — a colour hole vs master, worse than the race.
- **Do not add a feeder election dispatch** — struct-0-only symmetry above; also do not touch `encodeDepthWithFace` or the distance channel.
- **Body-split hygiene:** the default stage-2 wrapper must preprocess byte-identically (mind trailing lines/includes order — GLSL's include resolver is non-recursive; includes go in the wrapper, see stage-1's header note).
- **Metal:** single image-atomic scratch slot (16) is why the winner lives in a BUFFER, not an image (#2255 resolution). New buffer must untrack from Metal's sticky binding tables on destruction — standard `Buffer` teardown already does (#2412) — don't hand-roll a raw MTLBuffer.
- **The dup-scan rounds like the shader:** CPU `IRMath::roundHalfUp` (the GPU helper explicitly mirrors it), not `std::round`/`lround` — half-integer ties must land in the same cell on both sides.
- **Cost envelope:** flagged scenes pay one extra struct-0 dispatch + one buffer fill per flagged canvas per frame (matches #2255's accepted per-axis cost); unflagged scenes pay exactly zero — keep it that way (no unconditional binds/fills).
- **Host routing:** macOS builds are frozen (engine #2449) — implement + GL-verify on Linux; the issue's original Metal repro re-verifies via the macOS smoke lane once #2449 lands.

### Sibling / in-flight reconciliation

- **PR #2460** (fog vision Z cost, `fleet:wip`) edits `c_voxel_to_trixel_stage_1/2` fog regions — textual-merge overlap with the stage-2 body split. Whoever lands second rebases; keep the split content-preserving (pure move + `#if` additions) so the rebase stays mechanical.
- **PR #2393** (sun-shadow softness) touches shadow shaders only — no overlap. Open fleet-infra PRs (#2452/#2453/#2456/#2458) — no overlap.
- **#2255 / PR #2272 + #2429** (per-axis winner + scatter tie band) are merged; this plan extends their settled design to the store they explicitly left untouched, and its acceptance re-runs their criteria — no contradiction.
- **#2332 wave-freeze harness:** its metric-based gating remains valid; after this fix it CAN tighten to raw byte-identity — that is a separate follow-up filing, not in scope here.

### One task or subtasks

One task (fable). Single coherent stage-design change — flag + variants + election dispatch + both backends — mirroring the shape PR #2272 shipped as one PR. Splitting the backends or the flag out would ship an unverifiable intermediate.

