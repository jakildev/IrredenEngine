## Plan: render: per-axis (rotated) shots run-to-run non-deterministic on displaced-voxel scenes at high amplitude

- **Issue:** #2479
- **Model:** fable
- **Date:** 2026-08-04
- **Revision:** v2 — addresses the 2026-08-04 plan-review bounce on all four
  points (cap-floor cost claim, missing count instrumentation, underspecified
  guard, missing content-preservation criterion). The diagnosis sections the
  review marked "Not at issue" are carried over intact; every NEW claim below
  was re-verified against `origin/master` this pass.

### Scope

Make the per-axis view-visibility overflow lane's output a pure function of the
appended entry *set* rather than the append *sequence*, so rotated shots of a
static displaced scene are byte-identical run-to-run — closing the gap the
issue measured (10 distinct hashes / 10 runs on `zoom1_rot` / `zoom4_rot` /
`zoom4_rot_pan` at amplitude 5) without touching the cardinal fast path.

### Verified current state

Carried over from v1 (each cite independently re-verified by the plan review):

- **The issue's repro predates both related merges.** #2479 was filed
  2026-07-20 21:55Z; #2471 (θ-stable overflow membership) merged 2026-07-21
  02:57Z and #2478 (cardinal election) 03:54Z. The premise is unverified on
  current master, so phase 0 is mandatory, with a close-as-fixed bail.
- **Source-verified order-dependence map of the overflow lane**
  (`c_voxel_to_trixel_stage_1_body.glsl`): view-mask write `atomicMin`
  (commutative, ~line 283); append predicate deterministic given the mask
  (~line 338); **append index `atomicAdd` run-variant** (~line 349) — and entry
  index IS draw order (`v_peraxis_scatter.glsl` overflow branch indexes by
  `gl_InstanceID * 3u`, ~line 183); cap drop arrival-order-dependent (~lines
  350-357), with the drop counter read unconditionally CPU-side. The concrete
  tie shape: equal-`(cardPix, voxelDistance)` faces with different
  `colorPacked` — abundant at 8,011 colliding rounded cells — decode to
  identical quads whose equal-depth loser is decided by draw order; the
  `kScatterCellTieStep` layout block in `ir_iso_common.glsl` independently
  names this run-variant class.
- **The tie sub-band has zero spare bits** — the PRECONDITION brackets the band
  to exactly 16 steps ("15 <= 15: exact, zero slack"), so "widen the tie code"
  is not an available fix shape.
- **Sibling / in-flight reconciliation:** no open PR touches the overflow
  scratch, the mode-3 append, or the overflow scatter branch (#2758, #2475,
  #2393, #2385 surfaces checked). #2606 is complementary; #2334's
  `c_light_overflow_faces.*` reads per-pixel winners post-depth-test, so a
  canonical draw order feeds it deterministically too.

New this revision (verified against `origin/master` at `34c7f7f46`):

- **Cap sizing (review point 1):** `overflowCap_ = IRMath::max(axisCells / 4,
  65536)` (`component_per_axis_trixel_canvases.hpp:201`) — a **65,536-entry
  floor**, scaling with canvas area above it. The allocation derives from the
  same value (line ~205), and the entries region
  `[entriesBaseUints_, +overflowCap_*3)` sits above the ctrl block
  `[ctrlBaseUints_, entriesBaseUints_)` (indirect args + droppedCount), so an
  entries-region fill/sort cannot stomp ctrl. Entry word order is
  `{cell x|y (16+16), colorPacked, encodedDistance}` — the lexicographic
  `(cell, distance, color)` key is words `(0, 2, 1)`, as v1 stated.
- **Count instrumentation exists but is env-gated (review point 2):**
  `overflowCountLogEnabled_ = std::getenv("IR_OVERFLOW_COUNT_LOG") != nullptr`
  (`system_voxel_to_trixel.hpp:405`); `ctrl[1]` is the indirect-draw
  instanceCount = live entry count (~line 627), `ctrl[5]` the drop counter
  (line 613, read unconditionally).
- **IRMath has no integer power-of-two helper** — only the float
  `snapToPowerOfTwo` (`ir_math.hpp:424`). Phase 2 adds
  `IRMath::nextPowerOfTwo(uint32)`.
- **Profiling surface for the cost criterion:**
  `IR_PROFILE_SCOPE("voxelStage1")` / `IR_PROFILE_SCOPE("vs1_per_axis")`
  already bracket this exact path (`system_voxel_to_trixel.hpp:1156` / `641`)
  and feed the HUD / auto-profile numbers.

### Approach

**Phase 0 — re-measure the premise on current master (mandatory, cheap).**
As v1, with the review's instrumentation fix folded in: every run gets
`IR_OVERFLOW_COUNT_LOG=1`, so the `[overflow-count]` live `ctrl[1]` figure
lands in the captured stdout alongside the hashes:
`IR_OVERFLOW_COUNT_LOG=1 fleet-run IRPerfGrid --mode voxel_set --wave-freeze --no-overlay --grid-size 32 --zoom 0.8 --wave-amplitude 5 --auto-screenshot 80`
≥10 runs at amplitude 5; ≥3 each at amplitude 1 and 0 (same-scene controls).
Record per run: all 7 shot hashes, the live overflow count at the rotated
poses, and any drop warn. The live count is (a) the number phase 2's cost is
budgeted against and (b) the pre-fix half of acceptance criterion 6.
- *Expected (premise confirmed):* rotated shots differ across runs at
  amplitude 5; amplitude 0/1 and the 4 cardinal shots byte-identical.
- *Bail A:* rotated shots byte-identical 10/10 → fixed by the #2471/#2478
  window. Comment the measurement, recommend close, stop.
- *Bail B:* the amp-0/1 controls are NOT byte-identical → the byte-identity
  acceptance bar is unmeasurable in this environment. Comment and
  design-block — the acceptance criterion needs an architect ruling, not a
  quiet metric substitution.
- *Bail C:* the drop warn fires (`dropped > 0`) at amplitude 5 → the cap-drop
  sub-mechanism is live and the committed fix does not cover arrival-dependent
  *membership* (only order). Comment and design-block (cap raise vs
  keep-lowest election is an architect call).

**Phase 1 — localize to the overflow lane with the existing lever.**
Unchanged from v1: same amplitude-5 repro, ≥6 runs, with
`IR_PERAXIS_OVERFLOW_DISABLE=1` (draw-side skip,
`system_trixel_to_framebuffer.hpp:504`). Compare hashes *within* this
configuration only (the lever removes visible content — a known ~11 px
centroid shift — so cross-config comparison is meaningless).
- *Expected:* rotated shots byte-identical with the overflow draw off → the
  visible non-determinism is carried entirely by overflow entries.
- *Bail:* still non-deterministic → the mechanism is outside the overflow
  lane; comment the measurement and swap back to `fleet:needs-plan`.

**Phase 2 — the fix (committed): canonical-order the overflow entry list
before the overflow draw.** Same fix shape as v1 — sort the appended 3-word
entries by full value, key words `(0, 2, 1)` — with sizing and guard now
specified against the measured tree:

- **Power-of-two by construction.** Round the cap itself:
  `overflowCap_ = IRMath::nextPowerOfTwo(IRMath::max(axisCells / 4, 65536))`
  (new integer helper wrapping `std::bit_ceil` inside `engine/math`, the
  sanctioned home for std math). The 65,536 floor is already a power of two,
  so default-size canvases are byte-unchanged; the worst case grows the
  entries region ≤2× (3 uints/entry: a 262,144-entry cap is 3 MB → ≤6 MB).
  The append clamp, layout `.w`, the allocation, and the sort length are then
  one identical p2 value — no second size to drift.
- **Sentinel substitution, never thread-skip (review point 3).** Each rotating
  frame, before the sort, a fill pass writes
  `{0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}` into every entry slot in
  `[min(ctrl[1], cap), cap)`. The fill is mandatory: the region above the live
  range holds stale prior-frame entries, not zeros. Sort lanes never skip one
  side of a compare-exchange — out-of-live-range slots hold +∞ keys and
  participate normally, so the network stays valid and nothing can migrate
  garbage below `count`. (A real entry cannot tie the sentinel: word 0 packs
  iso cell x|y 16+16 and canvas cell coordinates never reach 65535, so a real
  all-max key is unproducible.) The only sanctioned shortcut: a compare pair
  whose slots are BOTH ≥ live count is provably +∞/+∞ and may no-op early —
  per-pass memory traffic then scales with the live count while the pass
  structure stays fixed.
- **Dispatch structure, honestly costed (review point 1).** The CPU cannot
  read `ctrl[1]` per frame without a sync stall, so the pass structure is
  sized to the p2 cap, not the live count. A naive one-dispatch-per-step
  network at the 65,536 floor is 16·17/2 = **136** barriered dispatches — the
  figure the review correctly refuted as "trivial". Committed structure: fuse
  every stride < workgroup size (256) into single shared-/threadgroup-memory
  dispatches — 1 local pre-sort, then per outer stage k∈[9,16] its (k−8)
  global steps + 1 fused local step = **45 dispatches** at the floor, each
  65,536 threads over ≤768 KB with the both-sentinel early-out above. That is
  the committed structure; its measured cost is gated by acceptance
  criterion 7, not assumed — phase 0's live count bounds the real traffic and
  the pre/post profile runs measure the delta.
- Unchanged from v1: in-place in the existing scratch (no new SSBO binding —
  the 0-30 budget is full), no encode/band change (zero-slack PRECONDITION
  respected), no cardinal-path change (the lane is rotating-only, so cardinal
  byte-identity holds by construction), the sort never touches `ctrl[1]`,
  dispatched from `system_voxel_to_trixel.hpp` after the mode-3 append and
  barriered before the indirect overflow draw (the same idiom the append→draw
  path already relies on). Register both shader paths in `shader_names.hpp`;
  the Metal twin is a top-level kernel file (AOT wrapper naming convention),
  needs `threadgroupSizeForFunctionName` sizing and `threadgroup` memory for
  the fused local stages.

**Phase 3 — verify** (acceptance below), authored on macOS/Metal (the repro
host) with the GLSL twin in the same PR; `fleet:needs-linux-smoke` covers the
GL host.

One task, one PR. Phases 0-1 are measurement-only (no build); phase 2 is the
only code phase.

### Affected files

- `engine/render/src/shaders/c_per_axis_overflow_sort.glsl` — new sentinel-fill
  + sort kernel
- `engine/render/src/shaders/metal/c_per_axis_overflow_sort.metal` — twin
- `engine/render/src/shader_names.hpp` — register the new shader
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — fill +
  sort dispatches + barrier after the mode-3 append
- `engine/prefabs/irreden/render/components/component_per_axis_trixel_canvases.hpp`
  — p2 cap rounding
- `engine/math/include/irreden/ir_math.hpp` — integer `nextPowerOfTwo`

### Acceptance criteria

1. **Positive-fire:** the exact amplitude-5 repro above, ≥10 runs —
   `zoom1_rot` / `zoom4_rot` / `zoom4_rot_pan` byte-identical, against the
   phase-0 measured red baseline (the pre-fix hash spread is the fire; fixture
   `IRPerfGrid` exists today).
2. Amplitude-0 and amplitude-1 runs stay byte-identical (≥3 runs each — #2255
   acceptance preserved).
3. The 4 cardinal shots stay byte-identical at amplitude 5 (no #2346
   regression).
4. Yaw-sweep temporal-stability gate per `engine/render/CLAUDE.md` (voxel
   cylinder, `jitter_probe --reversal-eps 0.8`, default 1.50px residual bar) —
   no regression vs the #2469 accepted-residual table.
5. Every verification run ends `ir-run: RESULT=CLEAN`.
6. **Content preservation (review point 4):** with `IR_OVERFLOW_COUNT_LOG=1`,
   the live `ctrl[1]` at the repro rotated poses is identical pre-fix vs
   post-fix (the sort reorders — it never adds, drops, or edits entries), and
   `ctrl[5]` stays 0 on both sides; AND post-fix, toggling
   `IR_PERAXIS_OVERFLOW_DISABLE=1` still produces the known ~11 px centroid
   shift vs enabled — the overflow entries demonstrably still draw.
7. **Measured cost (review point 1):** ≥3 runs pre-fix vs post-fix at the
   repro pose: the auto-profile `vs1_per_axis` and `voxelStage1` numbers and
   the total run wall time each regress < 5%. Report the measured numbers in
   the PR body either way.

### Gotchas

- `--no-overlay` is load-bearing for byte-identity (overlay timing text varies
  per run). Keep it in every measurement.
- The disable lever kills the *draw*, not the append — fine for the phase-1
  bisect, but never compare disabled-config hashes against enabled-config.
- Do not try to widen the tie sub-band code — the PRECONDITION block in
  `ir_iso_common.glsl` brackets the band to exactly 16 steps; the sort exists
  precisely to avoid that redesign.
- The sentinel fill runs every rotating frame BEFORE the sort — the region
  above `ctrl[1]` holds stale prior-frame entries, not zeros; skipping the
  fill feeds garbage into the network.
- Never thread-skip one side of a compare-exchange (the exact hazard the
  review named); only a both-sentinel pair may no-op.
- Keep the p2 rounding in the cap itself so the append clamp, layout `.w`,
  allocation, and sort length stay one value — do not introduce a separate
  `sortLen` that can drift from the cap.
- The ctrl block sits BELOW `entriesBaseUints_` — fill/sort index math is
  relative to the entries base; a base-offset bug lands in the indirect draw
  args, so bounds-check the region in the dispatch code.
- The sort must not touch `ctrl[1]` (instanceCount) and must complete before
  the indirect draw consumes the entries; use the same barrier idiom the
  append→draw path already relies on.
- Transient over-cap counter values are paired back by the append kernel;
  clamp the sort's and fill's count read to the cap anyway.
- `IrredenEngineTest` currently has 1 pre-existing failure on master (#2834,
  component-inventory count) — not this PR's surface; do not "fix" it here.


---

## Execution addendum — binding constraints from the 2026-08-04T06:32Z SOUND plan review

1. **Criterion 7's profile runs must be rotated.** A bare `--auto-profile` run
   sits at yaw 0 where `vs1_per_axis` reads ~0 on both sides. Pass `--yaw 0.35`
   (the shot table's rotated-pose yaw) on the criterion-7 runs and report the
   yaw used in the PR body alongside the numbers.
2. **Disclose the widened-cap side effect in the PR body.** On a canvas where
   `axisCells / 4 > 65536` and is not itself a power of two, the p2 rounding
   raises the append clamp (fewer drops → content change on drop-saturated
   scenes). State it explicitly so a later large-canvas content diff isn't
   mis-attributed to the sort.

---

## Revision v3 — architect ruling on the 2026-08-04T14:21Z design escalation

The v2 mechanism (canonical-order the overflow entry list) shipped and proved
correct — criteria 1-6 pass — but **criterion 7 failed as written**, and the
escalation argued the criterion is itself unsatisfiable by any multi-dispatch
design. The architect ruled **A+B** (2026-08-05T01:36Z, PR #2850 thread) and
accepted the escalation's analysis in full. This revision folds that direction
in; everything above stands except criterion 7, which is re-grounded below.

### 1. Criterion 7 is a gate defect — re-grounded (supersedes criterion 7 above)

A <5% budget on the 0.455 ms `voxelStage1` CPU row is a ~0.023 ms allowance
≈ 2-3 Metal encoder round-trips, which excludes every multi-dispatch design
including the plan's own committed mechanism. That is the
`engine/render/CLAUDE.md` **vacuous-failure mirror** shape: re-ground the
oracle rather than contort the mechanism. Replacement gate, measured at the
dense repro scene (`IRPerfGrid` wave-freeze, amp 5, `--yaw 0.35
--auto-profile`, the escalation's A/B protocol):

- **7a** — rotating frame-time delta **< 8%** vs pre-fix;
- **7b** — cardinal shots **~0** delta (within run-to-run noise);
- **7c** — **structurally zero added dispatches** when the pool is unflagged
  or the sort is otherwise gated off. Verify by **dispatch count / profile-row
  absence**, not timing alone.

### 2. (B) Gate the sort on `storeTiesPossible_`, backend-symmetric

The gating decision is CPU-side against the #2346 recomputed displaced-collision
flag — the same flag the cardinal winner election already gates on
(`system_voxel_to_trixel.hpp`, the `cardinalElection` local). **Both backends
take the same gate**, so the sort *semantics* never fork by backend. An
unflagged pool runs master's exact dispatch sequence: zero fill, zero network.

### 3. (A) Fuse where the cost lives — strided-slab shared memory

The cost is encoder round-trips, not arithmetic (a 16-byte partial-`subData`
variant measured identical). Fuse the network from 67 dispatches to **18** at
the repro scene's p2 cap (524,288):

- Fused block **2048 elements** (24 KB threadgroup: 2048 x 3 words x 4 B),
  the largest power of two under the 32 KB floor both backends guarantee.
  256 threads, 8 elements/thread for load+store, 4 compare-exchange pairs per
  thread per stride.
- **Generalized strided-slab pass.** A dispatch handles a *range* of stride
  bit positions `[pLo, pHi]` with `pHi - pLo + 1 <= 11` over a 2048-element
  slab closed under those strides. Compressed-index expansion:
  `i = ((c >> pLo) << (pHi+1)) | (active << pLo) | (c & ((1<<pLo)-1))`, with
  `c` the slab's index over all non-active bits. Workgroup count is
  `cap / 2048` for **every** stride group, independent of the group width.
  `pLo == 0, pHi == 10` degenerates to the contiguous-block case, so this one
  mode subsumes v2's separate mode 3 (local tail).
- **Resulting count** at cap `2^19`: 1 fill + 1 full local sort (stages
  `k = 2..2048`) + 2 dispatches for each of the 8 remaining stages
  (`k = 2^12..2^19`: top 11 stride positions, then the residual `s - 11 <= 8`)
  = **18**. Estimated ~+0.7 ms/rotating frame (~+5-6% frame) at ~40 us/encoder.

**Both backends get the fused network.** The ruling permits GL to keep the
unfused form pending a GL-host measurement, but that is a *permission*, not a
requirement: the fusion is a pass-structure change with identical semantics,
and forking the structure by backend would leave two networks to keep in
lockstep for no measured benefit (fewer barriers is not worse on GL either).
One structure, both twins.

### 4. Option C stays rejected

A value-derived float tiebreak inside the depth test carries cross-backend
float-identity risk and re-opens the zero-slack PRECONDITION ruling. Not
blessed; do not revisit under this issue.

### 5. Document the residual class, don't silently accept it

Unflagged pools keep order-resolved cross-cell band-code ties. State in
`docs/design/per-axis-trixel-canvas-rotation.md` §overflow lane: (a) the class
and why it is acceptable — membership is deterministic, no measured repro
exists (amp-1 / 35k entries was byte-identical pre-fix); and (b) the revisit
trigger: **a measured repro on an unflagged pool means widen the flag
recompute, never un-gate the sort.**

### Re-scoped acceptance

- Criteria 1-3 **re-verified post-fusion** on flagged pools.
- An explicit **unflagged-pool A/B** showing zero added dispatches (7c).
- Criterion 7 per the re-grounded gate (7a/7b/7c) above.
- Criteria 4 (jitter gate), 5 (clean exits), 6 (content preservation) as
  already specified.
- Screenshots, the doc updates in §5, and the widened-cap disclosure
  (addendum constraint 2) in the final PR body.

---

## Revision v3 — MEASURED OUTCOME (2026-08-05, macOS/Metal)

A+B is implemented exactly as ruled. **Every criterion passes except 7a**, and
7a fails by ~3x in a way that appears structural. Full evidence on the PR.

| Criterion | Result |
|---|---|
| 1 — amp-5 rotated determinism | **PASS** — all 7 shots byte-identical 10/10 runs, against a live master control measured the same session (3 distinct hashes / 3 runs on each rotated shot) |
| 2 — amp-0/1 controls | **PASS** — byte-identical 3/3 each |
| 3 — cardinal shots | **PASS** — all 4 byte-identical to master, not merely stable |
| 4 — jitter gate | **PASS (no regression)** — branch and master read digit-identical at zoom 2/4/8 (x residual 0.85 / 1.78 / 2.70 px). See the separate finding below: master itself now FAILS this gate's own 1.50 px bar at zoom 4/8, which is a pre-existing drift, not this PR's |
| 5 — clean exits | **PASS** — `RESULT=CLEAN` on all 40+ runs |
| 6 — content preservation | **PASS** — live `ctrl[1]` = 66,690 identical master vs branch; `ctrl[5]` = 0 both sides |
| 7b — cardinal ~0 delta | **PASS** — byte-identical, stronger than "within noise" |
| 7c — structurally zero dispatches when unflagged | **PASS** — `IRShapeDebug --spin-shape cylinder --spin-shape-voxel --yaw-sweep` reports `storeTiesPossible=false` → **0 dispatches**, while still carrying 72 live overflow entries (so the lane is active and the sort is provably absent, not vacuously untested) |
| **7a — rotating frame delta < 8%** | **FAIL — +24.2%** (9.89 → 12.28 ms p50) |

### The escalation's cost attribution was wrong, and that is the finding

The escalation attributed the cost to Metal's ~40 us encoder-per-dispatch round
trips, and the ruling approved fusion on that basis. Measured, 4 interleaved
runs per arm, same session, each arm asserted to be the binary it claims:

| arm | dispatches | frame p50 | vs master |
|---|---|---|---|
| A — master (no sort) | 0 | 9.89 ms | — |
| U — unfused (this PR's prior head) | 67 | 13.89 ms | +40.5% |
| B — fused + span-sized + gated | 18 (14 effective) | 12.28 ms | **+24.2%** |

The fusion is real and worth keeping — it cut the *added* cost from +4.00 ms to
+2.39 ms, a 40% reduction. But it does not reach 8%, and three separate
attributions were falsified by measurement along the way:

1. **Dispatch count is not dominant.** 67 → 18 moved the added cost only 40%.
2. **Memory traffic is not dominant.** Sizing the network to the live
   population instead of the cap cut traffic 4x (214 MB → 53 MB/frame; only
   12.7% of the cap was live) and moved frame time ~0.3 ms.
3. **Threadgroup occupancy is not dominant.** 24 KB → 6 KB per threadgroup at
   matched dispatch count and traffic moved nothing (3.02 → 3.34 ms GPU, i.e.
   slightly worse from the extra dispatches).

Fitting the three same-session variants gives a **~64 us fixed cost per
dispatch** that no in-kernel change touches. That makes the gate arithmetic
closed-form: 8% of a 9.89 ms frame is 0.79 ms ⇒ **at most ~12 dispatches, with
zero budget for anything else**. A bitonic sort over the live population
(nextPow2(66,690) = 131,072) at 2048-element workgroups needs **at least 14**.

So the re-grounded criterion 7 is, at this scene, in the same shape criterion 7
was before: **no correct implementation of the ruled mechanism can pass it.**
That is the `engine/render/CLAUDE.md` vacuous-failure mirror a second time, one
level down — and it is why this went back for a ruling rather than being tuned
further.

### Separate pre-existing finding (NOT this PR)

The `engine/render/CLAUDE.md` #2469 accepted-residual table is stale. It records
the voxel-cylinder yaw-sweep at x residual 0.57 px (zoom 4) and 1.25 px
(zoom 8); **master now measures 1.78 px and 2.70 px**, so the canonical probe
FAILS its own documented 1.50 px bar on master at both zooms. Branch and master
are digit-identical, so this PR is neutral on it. Filed separately.
