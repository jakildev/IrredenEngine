## Plan: render: polarity carrier for silhouette-riser faces — flipped faces light with inverted normals + per-axis store can't represent them

- **Issue:** #2207
- **Model:** fable — novel render-pipeline encoding design: a new carrier bit in the shared GPU distance encoding, constrained by atomicMin/Hi-Z depth ordering, a byte-identity invariant, and GL↔Metal parity. Wide surface, one-shot atomic migration, expensive to unwind.
- **Date:** 2026-07-08

### Verified current state + repro
- #2157 CLOSED, #2162 silhouette-riser flip MERGED — root cause is live on master.
- Distance texture is **R32I** (`component_triangle_canvas_textures.hpp:39`); the per-axis resolve texture shares that format (`component_per_axis_trixel_canvases.hpp:76`).
- Two encode helpers in the hub `ir_iso_common.{glsl,metal}`:
  - single-canvas `encodeDepthWithFace(depth, slot) = depth*4 + slot` (`ir_iso_common.glsl:123`) → `slot[1:0]`, `depth[31:2]`. CPU multiplier `kDepthEncodeShift = 4` (`ir_render_types.hpp:212`).
  - per-axis `encodeDepthWithFaceFrac(depth, slot, uFrac4, vFrac4) = (depth<<10)|(uFrac4<<6)|(vFrac4<<2)|slot` (`ir_iso_common.glsl:200`) → `slot[1:0]`, `vFrac[5:2]`, `uFrac[9:6]`, `depth[31:10]`.
- The store writes via **atomicMin** (nearest-depth wins) and a **Hi-Z max-depth** mip chain (`c_build_distance_hiz.glsl`) maxes the raw int. Depth is deliberately in the highest bits so raw-int min/max == depth min/max. **Consequence: a flip bit MUST live below depth** (a high-bit/sign-bit placement would let a flipped-but-farther cell win atomicMin → wrong occlusion, and would flip the Hi-Z max).
- The #2162 flip (`c_voxel_to_trixel_stage_1.glsl:385`, `stage_2.glsl:252`, + Metal twins) does `faceId ^= 1` (or dual-emits `oppositeFaceId = faceId ^ 1`). It mutates the `faceId` used for the stored **plane position** (`faceMicroPositionFixed6`, stage_1:649/671) and the downstream normal, **but the encoding still carries only the 2-bit slot** — the polarity flip is lost at the store. That single data-loss point causes both defects.

### Defect entanglement (why a partial fix can't clear the primary repro)
The dominant visual (`--debug-overlay shadow` venetian magenta rows) is on **gridspin** (rotated GRID) content, whose data path is: stage 1/2 per-axis raster (`encodeDepthWithFaceFrac`) → per-axis canvases → `c_resolve_per_axis_screen_depth` decodes per-axis and **re-encodes into the single-canvas layout** (`encodeDepthWithFace`, `:147`) → main distance → `c_lighting_to_trixel` decodes single-canvas → `faceOutwardNormal6(faceId)`. So the venetian fix (defect 1, on rotated content) requires the flip to survive the **per-axis** encode AND the resolve bridge AND the **single-canvas** encode. Defect 2 (per-axis scatter placement) requires the **per-axis** encode to carry the flip so `faceSpanCorner` spans the flipped `faceId`'s in-plane axes. The two defects share the per-axis carrier — they cannot be split into independently-shippable tasks.

### Decision: Option (b) — flip bit in BOTH distance encodings. Reject (a) and (c).
- **(a) entity-id carrier bit** (reuse the #2124 cut-face chokepoint): fixes ONLY defect 1. The per-axis **scatter** reconstructs the face plane from the **distance** texture's slot+depth (`v_peraxis_scatter.glsl:176-181`), never entity-id, so (a) cannot correct the mis-placed quad (defect 2). It also newly binds the entity-id texture into `COMPUTE_SUN_SHADOW` + `COMPUTE_VOXEL_AO` (2 kernels × 2 backends: shader decls + `system_compute_sun_shadow.hpp` / `system_compute_voxel_ao.hpp` binds), since today only `c_lighting_to_trixel` binds it. Since defect 2 forces a per-axis distance flip bit regardless, adding (a) on top means **two carriers for one polarity concept** → drift risk, strictly worse.
- **(c) consumer-side geometric inference**: heuristic, false-positives on adjacent geometry; cannot meet the strict `≤0.3%` + byte-identical acceptance. Reject.
- **(b)** is the single carrier that fixes both: the per-axis bit (unavoidable for defect 2) flows through the resolve re-encode into the single-canvas distance, and lighting/AO/shadow decode polarity from the distance texture they already bind everywhere. One source of truth, no new binds.

### Scope
Add a 1-bit "normal-flipped" polarity field to both distance encodings, propagate it from the stage-1/2 riser flip through the per-axis scatter + resolve bridge to every distance decoder, and make the polarity-sensitive consumers use the flipped outward normal / flipped in-plane span. Non-flipped content (`flip == 0`) must render **byte-identically** to master. One atomic PR (an encoding migration cannot land half-decoded).

### Approach (ordered; single branch, verify at the end)
1. **Centralize the layout in the hub, add a flip field.** In `ir_iso_common.{glsl,metal}`:
   - single-canvas: `encodeDepthWithFace(depth, slot, flip) = (depth<<3) | (flip<<2) | slot` (was `depth*4 + slot`). slot `[1:0]` unchanged, `flip[2]`, depth `[31:3]`.
   - per-axis: `encodeDepthWithFaceFrac(depth, slot, uFrac4, vFrac4, flip) = (depth<<11) | (flip<<10) | (uFrac4<<6) | (vFrac4<<2) | slot`. slot/vFrac/uFrac positions **unchanged**, `flip[10]`, depth `[31:11]`.
   - **Add shared decode helpers** — `decodeDepthSingle`, `decodeFlipSingle`, `decodeDepthPerAxis`, `decodeFlipPerAxis` (slot/`decodeSlot` already implicit as `& 3`) — and route every consumer through them so the bit layout lives in exactly one place. This is the safeguard against a missed shift across ~18 decode sites and makes future carrier changes cheap.
   - CPU mirror: `ir_render_types.hpp` `kDepthEncodeShift 4 → 8`, and update `depth_probe.hpp:145-146` (`%4`/`/4` → strip flip + `%8`/`/8`).
   - Depth headroom: single-canvas depth 30→29 bits, per-axis 22→21 bits; `depth = x+y+z` for canvas extents ≪ 2^20, so no overflow — assert/verify max depth during impl.
2. **Set the bit at the flip sites (stage 1 + stage 2, GL + Metal).** Where `faceId ^= 1` / `oppositeFaceId` is emitted, pass `flip = 1` into the encode (both the per-axis emit at stage_1:574/592, stage_2:327/341 and the single-canvas emit at stage_1:627/662/680, stage_2:392/422/441). Everywhere else pass `flip = 0`.
3. **Carry the bit through the per-axis scatter + resolve.** `v_peraxis_scatter`: decode `flip`, and when set span `faceSpanCorner` over the flipped `faceId = visibleFaceIds[slot] ^ 1`'s in-plane axes and place the plane accordingly (this is the defect-2 fix — see the "polarity once, in the store" contract in `docs/design/per-axis-trixel-canvas-rotation.md:135`). `c_resolve_per_axis_screen_depth` + `c_resolve_world_placed_depth`: decode the per-axis flip and re-emit it into the single-canvas `encodeDepthWithFace(..., flip)` so it survives the bridge.
4. **Make polarity-sensitive decoders flip-aware.** In `c_lighting_to_trixel`, `c_compute_sun_shadow`, `c_compute_voxel_ao` (GL + Metal), when `flip` is set negate the slot-derived outward normal (`faceOutwardNormal6`/`faceOutwardNormal6I`). The axis-only paths (`faceOutwardNormal(int)` at sun_shadow:195/203) are already sign-agnostic — verify each is genuinely polarity-invariant before leaving it unchanged. **Same-face gates**: `detectSelfStepStaircase` (`(neighbourEncoded&3)!=slot`, sun_shadow:119) and the AO same-slot exclusion/resample gates (voxel_ao:254/274) must treat a flipped neighbour as a **different** surface — extend the gate to compare `(slot, flip)`, not slot alone.
5. **Depth-only decoders: shift change only.** `c_fog_to_trixel`, `c_bake_sun_shadow_map`, `f_trixel_to_framebuffer` (monotonic depth × multiplier), `c_build_distance_hiz` (monotonic) route depth through the new helper; they ignore flip. `flip == 0` ⇒ identical decoded depth ⇒ byte-identical.
6. **Verify** (see acceptance). Use the staged-dir shader A/B swap to prove the `flip == 0` byte-identity invariant on the `--no-spin` sweep + shape_debug cardinals before evaluating the flipped-face fix.

### Affected files (cross-system audit — every producer/consumer of the shared encoding, GL + Metal)
**Hub / CPU (change first):**
- `engine/render/src/shaders/ir_iso_common.glsl` + `metal/ir_iso_common.metal` — encode helpers + new decode helpers + `flip` field.
- `engine/render/include/irreden/render/ir_render_types.hpp` — `kDepthEncodeShift 4→8`.
- `engine/prefabs/irreden/render/depth_probe.hpp:145-146` — CPU single-canvas decode.

**Encode producers (re-pack; pass `flip`):** `c_voxel_to_trixel_stage_1`, `c_voxel_to_trixel_stage_2` (these SET flip), `c_shapes_to_trixel`, `c_render_gpu_particles_to_trixel`, `c_render_stateless_particles_to_trixel`, `c_resolve_per_axis_screen_depth`, `c_resolve_world_placed_depth` — each `.glsl` + `metal/*.metal`.

**Polarity-sensitive decoders (add flip-aware normal / span / same-face gate):** `c_lighting_to_trixel`, `c_compute_sun_shadow` (main + `detectSelfStepStaircase`), `c_compute_voxel_ao`, `v_peraxis_scatter` / `metal/peraxis_scatter.metal` — each backend.

**Depth-only decoders (shift change only; flip-blind):** `c_fog_to_trixel`, `c_bake_sun_shadow_map`, `f_trixel_to_framebuffer` / `metal/trixel_to_framebuffer.metal`, `c_build_distance_hiz` (+ `metal/c_build_distance_hiz.metal`, `metal/c_chunk_occlusion_cull.metal`) — each backend.

Total edit surface ≈ **34 shader files (17 GLSL + 17 Metal) + 2 C++**. It is large but irreducible: an encoding migration must update every decoder atomically or master mis-decodes depth for all cells.

### One task or subtasks
**One task, one PR.** The two defects share the per-axis carrier (see entanglement), and a partial encoding migration breaks master. Not stackable.

### Acceptance criteria (from issue)
- Frozen-pose gridspin sweep (`--frozen-pose {0.3,0.6,0.9} --sweep-yaw 0 6.2832 17`): enclosed-black ≤ ~0.3% at ALL 17 directions (currently 1.77% @180°, ~0.4–1.2% non-cardinal).
- `--debug-overlay shadow` @180°: no venetian magenta rows on the rotated cubes; risers shade consistently with cube faces.
- Axis-aligned (`--no-spin`) sweep **byte-identical** at all 17 directions; shape_debug cardinal + yaw-0 shots byte-identical.
- GL + Metal parity.

### Gotchas
- **atomicMin / Hi-Z ordering:** flip must stay below depth (step 1). Do not touch `f_trixel_to_framebuffer`/`c_build_distance_hiz` monotonic assumptions beyond the shift.
- **Byte-identity is the review gate for the untouched paths:** every non-riser emit must pass `flip=0`; verify with the staged-dir shader A/B (byte-compare cardinal shots only — per-axis yaw shots are run-to-run nondeterministic, #2255).
- **Sibling reconciliation:** PR **#2275** (approved, awaiting windows-smoke) rewrites `c_compute_sun_shadow.glsl` / `c_lighting_to_trixel.glsl` / `ir_sun_projection.*` onto a unified sun-space basis — base this work on **post-#2275 master**; the flip-aware normal negation layers on the unified normal. PR **#2278** (currently `fleet:design-blocked`) edits stage-2 emit + `ir_render_types.hpp` + `c_voxel_visibility_compact` — if it lands first, re-check the stage-2 flip site and the entity-id/`reserved` bit usage for conflicts.
- **Same-face gates** (AO exclusion, self-step): a flipped neighbour is a distinct surface; comparing slot alone will mis-merge it. Extend to `(slot, flip)`.
- **frac untouched:** per-axis frac fields do not move (flip goes at [10], above frac); only the depth shift and the new flip decode change. Don't perturb `(rawDist>>2)&15` / `(rawDist>>6)&15`.
- **Metal parity:** every GLSL edit has a 1:1 Metal twin; the Metal-backend files are excluded from clang-format (edit style-by-hand to match).

