## Plan: render: shadow-feeder survivors win viewport-edge pixels whose color tap stage 2 skips — #2488 secondary observation reproduces on GL (4,128 px)

- **Issue:** #3010
- **Model:** opus — plan review C2: the plan originally re-picked `sonnet`, but
  the issue body declares `opus` and `fleet:opus` is the label the queue carries,
  so the two are reconciled toward the applied label. Needs a GL host to run the
  gate (`needs_gl_host`).
- **Date:** 2026-09-06

### Scope

Answer the issue's first acceptance criterion with a measurement rather than an argument, and leave the answer guarded. The measurement (below) says the edge pixels are **correct by design**: at the shipped 4-iso-px margin no shadow feeder wins an on-screen pixel, and the issue's 4,128-pixel delta is the sun shadow the force-culled casters stopped casting. "Done" is: the determination recorded in-tree, the two in-source claims that overstate it corrected, and a positive-fire gate committed so a future change to the emission hull or the margin cannot silently reopen the question. No render behavior changes at default.

### Verified current state

Code pins are against `origin/master @ f3e79a54`; measurements are from this planning session on the Windows/OpenGL/NVIDIA host with the issue's exact recipe.

- **One classification predicate, two consumers.** `isShadowFeederIso(isoPos, visibleIsoBounds, residualYaw, isDetachedCanvas)` (`engine/render/src/shaders/ir_iso_common.glsl` ~453-481, Metal twin) is called by the compact's Step-B classify (`c_voxel_visibility_compact.glsl` ~389-445, `isoPos = pos3DtoPos2DIso(roundHalfUp(positions[idx]))` at ~351) and by stage 2's depth-only skip (`c_voxel_to_trixel_stage_2_body.glsl` ~381-412, `feederPos = roundHalfUp(voxelPosition.xyz)`). Both round with `roundHalfUp`, as does stage 1's cardinal NONE emit (`c_voxel_to_trixel_stage_1_body.glsl` ~752), so the classify centre and the raster base agree cell-for-cell even for the fractional `--wave-freeze` positions.
- **The box.** `visibleIsoBounds_` is `floor/ceil` of `IRRender::getCullViewport().isoViewport(kGpuMargin)` with `kGpuMargin = 4` (`engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` ~1460-1496). `kGpuMargin` has exactly three readers, all in that block: the un-widened box, the sun-widened cull box (`shadowFeederCullViewport`), and nothing else (`grep -n kGpuMargin engine/` → 3 hits, one file). `visibleIsoViewport` adds the margin in iso units, unscaled by zoom (`engine/math/include/irreden/ir_math.hpp` ~1236-1248); at `SubdivisionMode::NONE` one iso unit is one canvas texel.
- **The footprint.** On the cardinal NONE path stage 1 emits each voxel through `emitDeformedFace(base, D, …)` with identity `D` (residualYaw == 0), `n = 1`, over the `[0,2)x[0,3)` invocation lattice: the voxel's whole write set is `base + {0,1} x {0,1,2}` (`c_voxel_to_trixel_stage_1_body.glsl` ~395-441, ~774-783). Reach from the classify centre is +1 texel in x and +2 in y, and 0 toward −x/−y. The 4-texel margin therefore covers it with 2-3 texels of slack on the two sides where the reach points into the viewport.
- **Why the issue's probe reads 4,128.** Force-culling every ring feeder (dropping the struct-1 tail-append) removes their **stage-1 depth**, so the sun bake loses every off-screen caster; on-screen shadow pixels change wherever those casters' throw landed. That intervention is shadow-confounded by construction. The issue's own numbers show it: 48% of the changed pixels lie more than 16 px from the frame edge (y reaches 571), far beyond any 2-texel footprint reach — that is shadow-throw geometry.
- **Shadow-neutral measurement (this session).** A staged pad on `visibleIsoBounds_` **only** (cull box, Hi-Z cull, and the #2488 ring guard untouched) converts band voxels between feeder and visible while every voxel's stage-1 depth is identical in both arms (`feederSubCap == subdivisions == 1` at NONE), so a pixel can change only through the stage-2 colour/entity-id tap. Recipe: `IRPerfGrid --mode voxel_set --no-overlay --subdivision-mode none --wave-freeze --wave-amplitude 5 --occlusion-cull --auto-screenshot 10`, shot `zoom4_pan` (zoom 4, offset (16,8), yaw 0), every run `ir-run: RESULT=CLEAN`, compared pixel-exact against an unmodified baseline run:

  | pad on `visibleIsoBounds_` | meaning | `zoom4_pan` changed px | other cardinal shots |
  |---|---|---|---|
  | +12 | classify box 12 texels wider than shipped: band feeders become visibles | **0** | fit_grid 0, zoom1_origin 0, profiler_overlay 0 |
  | −4 | box = exact on-screen viewport (margin 0) | **0** | fit_grid 0, zoom1_origin 0 |
  | −16 | box 12 texels *inside* the viewport: on-screen winners become feeders (liveness control) | **103,168** — a contiguous band along the top, right and bottom edges where the grid reaches the frame; 0 on the left edge, which the grid never reaches | zoom1_origin 0 (the grid is nowhere near the band at zoom 1) |

  The +12 row is the determination: nothing on screen is resolving from a depth-only feeder, so there is no colour-tap gap for the margin to be inadequate against. The −16 row proves the instrument fires when feeder-won on-screen pixels exist. The −4 row says the margin is not even load-bearing on this scene at NONE. The +12 and −4 shots hash byte-identical to the baseline (md5 `b281b0137c035a95c35ab70472cc8bdf` for all three; the −16 shot is `2764de97c61362f0ec7e5f2ecf2f8a56`). The issue's `d604766831b6666bdd919d343de06c0f` was taken on an older master with #3005 applied locally, so it differs from this session's baseline by construction; every identity claim here is within one build of `f3e79a54`.
- **The stale claims.** `c_voxel_to_trixel_stage_2_body.glsl` ~381-395 (and the Metal twin ~391-430) say "The +4-iso-px margin baked into visibleIsoBounds covers the voxel face footprint, so no on-screen pixel is ever a feeder"; `system_voxel_to_trixel.hpp` ~1957-1961 repeats "#1740's margin guarantees no on-screen pixel is a feeder"; `docs/design/voxel-feeder-split.md` §Q3 lists stage 2's colour tap as a visible-region reader inside `isoViewport(kGpuMargin)`. All three are true today, by the measurement above, and none of them is guarded — the emit hull has a KEEP-IN-SYNC obligation toward the Hi-Z window (`voxelOccludedByHiZ`) but none toward the margin.
- **Readback primitive exists** if a future probe wants GPU-state rather than screenshots: `Texture2D::getSubImage2D` (used by `IRRender::readbackCompositeDepth`, `engine/render/src/ir_render.cpp` ~96-139) over `C_TriangleCanvasTextures::getTextureDistances()` / `getTextureEntityIds()`. Not needed for this plan (see Decisions).
- **In-flight reconciliation.** No open PR touches `system_voxel_to_trixel.hpp`, `c_voxel_visibility_compact.*`, `ir_iso_common.*`, `c_voxel_to_trixel_stage_2_body.*`, `sun_shadow_constants.hpp`, or `perf_grid/main.cpp` (checked 2026-09-06). #2488 (Metal scratch resolve) is merged; its plan explicitly routed this question here.

### Decisions

- **D1 — Determination: correct by design.** At the shipped margin, on the world canvas at NONE, no shadow feeder wins an on-screen pixel; the 4,128-px force-cull delta is lost sun shadow from casters that were removed from stage 1, which is the feeder ring working as designed. No margin change, no shader change. The issue closes on this determination plus the gate below.
- **D2 — The force-cull probe is retired as evidence.** It is shadow-confounded (it removes depth, not just colour). The plan file, the design doc note, and the harness all name the pad probe as the only valid instrument for this question.
- **D3 — Gate instrument: a runtime classify pad, applied to `visibleIsoBounds_` and nothing else.** Public surface: `IRPrefab::SunShadow::setFeederClassifyPadIso(int)` / `feederClassifyPadIso()` in `engine/prefabs/irreden/render/sun_shadow_constants.hpp` (the file that already owns `shadowFeederCullViewport` / `shadowFeederRingNonEmpty`). Storage is **system-owned state on `VOXEL_TO_TRIXEL_STAGE_1`**, reached the #2526 way (`IRSystem::findSystem(VOXEL_TO_TRIXEL_STAGE_1)` + the system's params instance), in whichever params form that system already uses — never a field on `IRRender` / `RenderManager` (render CLAUDE.md §"What belongs in engine/render/"), never a header global (`.claude/rules/cpp-globals.md`). Default `0` ⇒ the frame data is bit-identical to master. The pad is applied after the floor/ceil at ~1493 as `ivec4(-p, -p, +p, +p)`; `cullIsoMin_/Max_`, the Hi-Z cull, and the `shadowFeederRingNonEmpty(gpuVp, visibleVp)` guard keep reading the unpadded values. Negative values are allowed — they are the liveness control, and they are test-only.
- **D4 — One creation surface: `IRPerfGrid --feeder-classify-pad <N>`** (IRArgs `.integer`, default 0). The demo calls D3's setter after `initSystems` has created `VOXEL_TO_TRIXEL_STAGE_1` (the setter resolves the system through `findSystem`, so it has nothing to bind to before that), next to where `--occlusion-cull` / `--no-per-voxel-occlusion` already reach the render systems.
- **D5 — Harness: `scripts/feeder-margin-verify.py`, shaped like `scripts/cull-verify.py`.** Build → three `IRPerfGrid` runs with the recipe above at pads `0`, `+12`, `−16` → compare `zoom4_pan` (and `fit_grid`, `zoom1_origin`) pixel-exact with `render-compare.py`'s `read_png`. Verdict rules: the `−16` arm MUST differ from `0` on `zoom4_pan` (liveness; fail as "vacuous instrument" if it does not), the `+12` arm MUST be byte-identical to `0` on every compared shot (adequacy; fail as "feeder-won on-screen pixels" with count and bbox if it does not), **and the `0` arm MUST report a non-empty shadow-feeder ring** (adequacy-arm non-vacuity — plan review C1; fail as "vacuous adequacy arm — zero feeders to promote"). Only cardinal shots are compared — the rotated shots are run-to-run non-deterministic on this demo (3 hashes in 3 runs, per the issue). Exit 0 only when all three rules hold. `--no-build`, `--build-dir`, `--warmup`, `--timeout` mirror `cull-verify.py`; **`--liveness-pad <N>` (default `−16`)** is the switch acceptance criterion 2 drives. Pure logic (the pixel census, the verdict) lives in importable functions with a `scripts/tests/test_feeder_margin_verify.py` unit test (the #3063 gap: `scripts/*.py` helpers with no direct test).
- **D6 — Comment and doc corrections, in the same PR.** Replace the two "no on-screen pixel is ever a feeder" assertions with the measured statement: the NONE-path write set is `base + {0,1}x{0,1,2}` (reach +1/+2 texels), the 4-texel margin covers it with slack, and `scripts/feeder-margin-verify.py` is the gate — plus a KEEP-IN-SYNC pointer from `emitDeformedFace` to the margin, mirroring the one it already carries toward `voxelOccludedByHiZ`. Add a short "measured 2026-09-06" note to `docs/design/voxel-feeder-split.md` §Q3 with the table above and the shadow-confound caveat.
- **Out of scope.** Shadow deltas from culled casters (by design). Changing `kGpuMargin`. Subdivided-mode adequacy (`FULL` / `POSITION_ONLY`): the same ratio holds by construction (positions and margin both scale by `subdivisions` in `trixelFrameOffset` / `isoViewport`), but it is not measured here and is not this issue's recipe — the harness gains a `--subdivision-mode` passthrough so a later ticket can run the same three arms there. Metal: no shader touched; the #2488 resolve already carries ring depth into the texture there, and cross-host smoke covers the demo run.
- **Rejected alternatives.** (a) A GPU-state probe counting on-screen texels with `distance != 65535 && entityId == 0` via `getSubImage2D`: it measures the same population the screenshot A/B already isolates, needs two texture readbacks plus an on-screen-texel mapping, and still needs the pad knob to positive-fire — more code for no more evidence. (b) Deriving a classify pad from the emission hull automatically, the way `voxelOccludedByHiZ` derives its window: nothing needs fixing, so a derived pad would be dead weight on a hot path. (c) Closing the issue with the measurement alone: leaves the in-source claim unguarded; the vacuous-failure rule in `engine/render/CLAUDE.md` §"Verifying render changes" is exactly the case this gate prevents.

### Affected files

- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` — pad + ring-witness storage on the system; apply at ~1493; the `IRPrefab::SunShadow::` accessor block (`setFeederClassifyPadIso` / `feederClassifyPadIso` / `feederClassifyRingNonEmpty`) at the bottom, per the placement note below; correct the comment at ~1957-1961
- `engine/render/src/shaders/c_voxel_to_trixel_stage_2_body.glsl` and `…/metal/c_voxel_to_trixel_stage_2_body.metal` — comment correction only
- `engine/render/src/shaders/c_voxel_to_trixel_stage_1_body.glsl` (+ Metal twin) — KEEP-IN-SYNC pointer on `emitDeformedFace`, comment only
- `creations/demos/perf_grid/main.cpp` — `--feeder-classify-pad`
- `scripts/feeder-margin-verify.py` (new), `scripts/tests/test_feeder_margin_verify.py` (new)
- `docs/design/voxel-feeder-split.md` — §Q3 note
- `.fleet/plans/issue-3010.md` — this plan, first commit of the PR

### Acceptance criteria

1. **Positive fire (the gate):** `python3 scripts/feeder-margin-verify.py` on a GL host exits 0 and prints the −16 arm's `zoom4_pan` changed-pixel count > 0 (this session: 103,168, an edge band) and the +12 arm's count == 0 on every compared shot. Cite the printed numbers in the PR body.
2. **Vacuity guard is live:** running the harness with `--liveness-pad 0` exits non-zero with the "vacuous instrument" verdict. Cite the run.
2b. **Adequacy-arm non-vacuity guard is live (plan review C1):** the pad-`0` arm reports a non-empty shadow-feeder ring, and a run where it does not exits non-zero with a *distinct* "vacuous adequacy arm" verdict. Covered by the unit test's synthetic case; cite it.
3. **Byte-identity at default:** the pad-0 `zoom4_pan` shot is byte-identical to the same shot from a master build of the demo (md5 in the PR body), and `render-verify` on the shape_debug reference set is unchanged.
4. **Unit test:** `scripts/tests/test_feeder_margin_verify.py` passes and covers the census and both verdict rules with synthetic images (including the vacuous case).
5. **Comments/docs:** the two "never a feeder" assertions are gone; the design-doc note carries the table and the confound caveat.
6. Every demo run in the PR body ends `ir-run: RESULT=CLEAN`.

### Gotchas

- The pad must land on `frameData_.visibleIsoBounds_` only. Padding `visibleVp` itself would also move the #2488 ring guard and the occlusion pre-pass's box; padding `cullIsoMin_/Max_` would change which voxels raster at all and break the depth-identical property the measurement rests on.
- `feederSubCap == subdivisions` at NONE is what makes feeder and visible depth identical; the harness's default recipe stays at `--subdivision-mode none` for that reason.
- `save_files/screenshots` numbering continues from leftovers (`VideoManager::reserveNextScreenshotIndex`); the harness must park or wipe the directory between arms, as `cull-verify.py` / the jitter recipe do.
- Compare cardinal shots only; the yaw-0.35 shots are non-deterministic on this demo.
- `--occlusion-cull` is part of the issue's recipe and is byte-identical for `zoom4_pan` either way; keep it so the numbers stay comparable with the issue.
- No engine-library translation unit includes `system_voxel_to_trixel.hpp` (only prefab headers and the demo `main.cpp` do), so the demo binary carries a single definition of the system — a knob added there is the one that runs. Re-check that census if the system ever moves into a `.cpp`.
- Plan and PR are engine-public: engine terminology only.

### Plan-review corrections (folded in)

Plan review verdict **sound with corrections** (opus-reviewer, pool-3,
2026-09-06 — full text on issue #3010). All four are folded into the sections
above; recorded here so the in-tree plan matches the thread:

- **C1 — the `+12` adequacy arm needs its own non-vacuity witness.**
  `isShadowFeederIso` tests only against `visibleIsoBounds` (no upper cull-bound
  term), so the two arms are asymmetric: `−16` fires even with an empty feeder
  ring, while `+12` can only change a pixel if there were feeders in the
  `[visible+4, visible+16)` band to promote. With sun shadows off the ring is
  empty, `+12` reads 0 tautologically, and the harness still exits 0. Folded
  into D5 as a third verdict rule and acceptance criterion 2b. **Implementation
  choice (C1 offered three; this is the second):** assert the pad-`0` arm's ring
  is non-empty via the existing `shadowFeederRingNonEmpty` — the system already
  computes it per canvas at the unpadded boxes, so the witness is one accumulated
  bool and no GPU readback.
- **C2 — `**Model:**` conflict** between the issue body (`opus`) and the plan
  (`sonnet`). Reconciled toward the applied `fleet:opus` label; see the header.
- **C3 — acceptance criterion 2's undecided fork.** The switch is
  `--liveness-pad <N>` (default `−16`), now in D5's flag list.
- **C4 — precision on two cited sites.** The quoted phrase *"no on-screen pixel
  is ever a feeder"* has exactly two in-source hits
  (`c_voxel_to_trixel_stage_2_body.glsl:391`, `system_voxel_to_trixel.hpp:1959`);
  the Metal twin's equivalent claim is worded differently
  (`metal/c_voxel_to_trixel_stage_2_body.metal` ~397-399, *"visibleIsoBounds
  carries a +4-iso-px margin covering the face footprint"*) and still needs
  correcting. `kGpuMargin` is 1 definition + 2 readers, not 3 readers.

### Implementation deviation from D3 (file placement only)

D3 names `sun_shadow_constants.hpp` as the home for
`setFeederClassifyPadIso` / `feederClassifyPadIso`. That placement is not
buildable: `system_voxel_to_trixel.hpp` **includes**
`sun_shadow_constants.hpp` (for `shadowFeederCullViewport`), so a setter that
resolves `IRSystem::System<VOXEL_TO_TRIXEL_STAGE_1>` from the constants header
closes an include cycle. `engine/prefabs/CLAUDE.md` §"Component method rules"
names this exact case ("if the feature's main header includes its own system,
the accessors need their own third header"), and the in-tree precedent for a
`findSystem`-resolved handle is to put the `IRPrefab::<Feature>::` block at the
**bottom of the system's own header** — `IRPrefab::VoxelTransform::allocator()`
in `system_update_voxel_positions_gpu.hpp:301-318` and
`IRPrefab::JointTransform::system()` in `system_update_joint_matrices.hpp:419`.
The accessors follow that precedent: same `IRPrefab::SunShadow` namespace, same
`findSystem` + `getSystemParams` resolution, same
`kNullSystemId` ⇒ no-op contract. Everything load-bearing in D3 — system-owned
storage on `VOXEL_TO_TRIXEL_STAGE_1`, the #2526 resolution, default `0`, pad
applied to `frameData_.visibleIsoBounds_` only, no `IRRender`/`RenderManager`
field, no header global — is unchanged.

### Approach sketch (advisory)

Land the plan file, then the knob (D3) and flag (D4), and reproduce the three-arm table by hand once before writing the harness — if the −16 arm reads 0 on the implementer's host, stop and comment the measurement on the issue instead of building on it. Then clone `cull-verify.py` into the harness, write the unit test against the extracted census/verdict functions, and finish with the comment/doc corrections. One PR, `Closes #3010`.
