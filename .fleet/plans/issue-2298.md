# Plan: render — extend per-voxel occlusion cull domain to the shadow-feeder-widened canvas

- **Issue:** #2298
- **Model:** opus — bounded GPU compute change on the shipped #1812 mechanism
  (gate-domain relax + invariant verification), same tier as #1812 itself; the
  mechanism and invariant argument are already settled, so not fable.
- **Date:** 2026-07-13
- **Implemented by:** PR #2475 (merged 2026-08-22). This file is the plan of
  record, committed after the fact — #2475 carried the implementation but not
  the plan file its own step 5 called for.

### Verified current state + measured planning gate (premise corrections)

- **The issue body's "zoom8 captures ~nothing (0.8119 → 0.8117)" is from the
  zero-fire era and is falsified.** PR #2278 merged 2026-07-10 after
  root-causing the Metal image-shadows-sampler bind; the per-voxel cull now
  fires: zoom8 `--no-sun-shadows` marginal capture is 147–149K voxels (88.8% of
  visible-domain survivors).
- **The Hi-Z already covers the feeder-widened canvas.**
  `System<COMPUTE_DISTANCE_HIZ>` (`system_build_distance_hiz.hpp`)
  downsample-maxes the **full** R32I distance texture — the canvas the feeders
  raster into. No new Hi-Z build infrastructure or larger-region build cost is
  needed; the "Hi-Z covers only the visible viewport" wording in the #1812
  plan's gotcha is imprecise (the *test gate*, not the data, is visible-only).
- **The visible-only restriction is one early-return**: `voxelOccludedByHiZ` in
  `c_voxel_visibility_compact.glsl` returns false when `isoPos` is outside
  `visibleIsoBounds` (binding-7 UBO, offset 176). Everything else (Hi-Z image
  bind at unit 1, `encodeDepthWithFace` encoding, `kOcclusionDepthMargin`,
  NONE-mode/cardinal gating, camera-cut disable) is domain-agnostic and stays
  as-is.
- **Measured planning gate** (Metal host, `IRPerfGrid --mode voxel_set
  --no-overlay --auto-profile --subdivision-mode none --wave-amplitude 0
  --occlusion-cull --zoom 8` ± `--no-per-voxel-occlusion` ± `--no-sun-shadows`;
  300-frame `avgVisible`, pool 262144):

  | regime | chunk-only | chunk+per-voxel | pv marginal |
  |---|---|---|---|
  | shadows ON (widened) | 213494 | 64737 | 148757 |
  | shadows OFF (visible-only) | 167507 | 18715 | 148792 |

  - Feeder-ring survivor population = 213494 − 167507 = **45987 voxels (21.5 %
    of the widened survivor set)**.
  - Post-cull, ring-kept voxels are 64737 − 18715 = **46022 ≈ 71 % of the
    remaining survivor set** — the dominant residual, exactly the population
    this issue targets.
  - The pv marginal is identical shadows-ON vs OFF (Δ35 voxels): the shipped
    cull provably cannot touch the ring.
  - If in-ring capture matches the visible-domain rate (88.8 %), survivors fall
    64737 → ~24K (a further ~63 % cut). Even at half that rate the prize is
    ~20K voxels/frame plus their stage-1 micro-slice dispatch cost.
- **Cost model (confirmed):** zero new build cost (Hi-Z already full-canvas);
  the added per-frame cost is the same one-imageLoad-plus-compare per ring voxel
  already paid per visible voxel — ~46K extra tests at zoom8, on a path that
  today early-outs at the bounds check. Off-by-default cost stays zero
  (`occlusionCullMipCount == 0` short-circuits before any of this).

### Mechanism picked

**Primary mechanism from the issue body — widen the per-voxel test domain to
the feeder-widened canvas.** The sun-space fallback is NOT planned: the ring
population is directly reachable by the existing test (Hi-Z data is real in the
ring — feeders raster there), and the architect already endorsed the invariant
argument on #2278. If realized in-ring capture comes in far below the
visible-domain rate (see Gotchas), the residual becomes a new
measurement-first issue, not mid-PR improvisation.

**Soundness invariant (supersedes the #1812 "never cull a feeder"
mitigation):** a voxel that is conservatively occluded (expanded-footprint Hi-Z
max, strict-behind margin) at every canvas texel it can raster to leaves no
trace in `trixelDistances`. Both consumers — the visible resolve AND the
sun-shadow bake — consume `trixelDistances`, never voxels, so dropping such a
voxel is bit-identical for both. A camera-occluded-everywhere feeder already
casts nothing; the #1812-era hazard ("dropping a camera-occluded but
shadow-relevant caster loses its sun shadow") only applied while the test
data/domain was assumed visible-only.

### Sibling + in-flight reconciliation

- **PR #2325 (#2258 Step B) edits the same compact append block**: it
  classifies survivors visible-vs-feeder on the same `visibleIsoBounds`
  boundary and tail-appends feeders into a second indirect struct. Composition
  is clean — the pv test runs *before* classification/append, so a culled ring
  voxel simply never enters the feeder tail list, shrinking the feeder stage-1
  dispatch (`feederSubCap²` micro-slices per dropped voxel). **Base the impl
  branch on master after #2325 merges** (same shader region; rebasing across it
  is avoidable churn). `visibleIsoBounds` must NOT be removed — #2325's
  classification still consumes it.
- **#2360** (pre-existing #1294 chunk-cull fine-level corruption on Metal) —
  orthogonal; it is why all A/B gates below use the **marginal isolation** form
  (chunk+pv vs chunk-only), never cull-on vs cull-off.
- **#2361** (render-verify baseline for the cull-firing regime) — related
  follow-up, not a blocker.
- `Blocked by: #1812` is resolved (PR #2278 merged).

### Approach (single committed path)

1. **Relax the domain gate in `c_voxel_visibility_compact.glsl`.** In
   `voxelOccludedByHiZ`, replace the `visibleIsoBounds` early-return with a
   **canvas-coverage guard**: test only voxels whose 1-texel-expanded footprint
   lies fully inside the Hi-Z texel extent (derive from the level-0 image size
   or pass the widened canvas texel bounds via an existing UBO lane). A
   footprint that would clamp at the canvas edge keeps the voxel — clamped
   reads sample the wrong texel and are not conservative.
2. **Metal mirror** in `c_voxel_visibility_compact.metal`, identically. No new
   kernel, no threadgroup-map entry.
3. **CPU side (`system_voxel_to_trixel.hpp`)**: no new bind (Hi-Z image bind at
   unit 1 already happens whenever the cull is armed); adjust/add the
   widened-bounds UBO lane only if step 1 needs it (prefer reading the image
   extent shader-side if the backends agree). Keep every existing arming
   condition unchanged.
4. **Docs**: update `docs/design/voxel-occlusion-culling.md` — supersede the
   "#1 correctness hazard" mitigation with the trace-invariant argument (record
   it as lesson-adjacent prose next to lessons (h)/(i)), and record the
   planning-gate table above.
5. **Plan file**: commit this plan as `.fleet/plans/issue-2298.md` (first
   commit of the impl PR, #1932 convention).

### Affected files

- `engine/render/src/shaders/c_voxel_visibility_compact.glsl` — domain-gate
  relax + canvas-coverage guard (core change)
- `engine/render/src/shaders/metal/c_voxel_visibility_compact.metal` — Metal
  mirror
- `engine/prefabs/irreden/render/systems/system_voxel_to_trixel.hpp` —
  widened-bounds UBO lane only if needed; otherwise untouched
- `docs/design/voxel-occlusion-culling.md` — invariant + measurement record
- `.fleet/plans/issue-2298.md` — plan file (new)

### Acceptance criteria

- **Ring positive-fire gate (required):** shadows-ON marginal capture must
  EXCEED the same-build shadows-OFF marginal at zooms 1/4/8 (the excess is by
  construction ring capture — the table form above). Record the numbers; Δ≈0 =
  the ring test never fired = not done. (Lesson (i): identity gates require a
  paired positive-fire gate.)
- **Marginal byte-identity with sun shadows ON (required):** chunk+pv vs
  chunk-only md5 A/B across the perf_grid shot table with shadows ON — the bake
  path is now in-domain, so the shadows-ON A/B is the load-bearing identity gate
  (the #1812 gate ran shadows-off and cannot see a bake regression). Also re-run
  the shadows-OFF A/B (must stay identical — the visible-domain behavior is
  untouched).
- **Cast-shadow spot check:** an off-screen caster scenario (mirror #2325's
  render-debug-loop gate) shows no shadow loss with the widened cull armed.
- **Byte-identical when off:** default `occlusionCullMipCount == 0` path
  untouched; `--no-per-voxel-occlusion` still isolates the refine.
- **Post-#2325 composition check:** with the feeder partition present, the
  feeder indirect count drops when the widened cull arms (read back via the perf
  HUD CULL block / DOMAIN-STATE log).
- Realized `voxelStage1` (+ feeder dispatch) reduction on `voxel_set` zoom8
  recorded in the PR body; perf ms on the Linux/GL host if the authoring host is
  Metal (timer rows read 0.000 there — capture stats are the Metal gate).
- Both backends build; cross-host smoke labels per convention.

### Gotchas

- **Canvas-edge clamping is the correctness cliff of step 1** — a clamped
  `hiZTexel` read is data for a *different* position; the coverage guard must
  exclude, not clamp. Mirror how the frustum test handles `cullMargin`.
- **#2325's strided feeder raster sparsifies ring Hi-Z content**: fewer writes →
  more 65535 sentinel texels → footprint max stays huge → conservative keeps.
  Correct but capture-weakening. This is the issue body's "footprint-max dead
  zone at feeder scales" — measure it, don't fight it in this PR; a
  far-below-visible ring capture rate becomes the follow-up measurement issue
  (possibly the sun-space mechanism), filed per TASK-FILING.md.
- **One-frame Hi-Z lag**: unchanged; the existing `occlusionLagSourceStale_`
  camera-cut disable covers the widened domain too (the ring pans with the
  camera; no new discontinuity source).
- **Do not touch `system_build_light_occlusion_grid`** (lighting invariant 1 —
  full-pool iteration, never the compacted list).
- **Do not remove or repurpose `visibleIsoBounds`** — #2325's visible/feeder
  classification reads it; only the cull's early-return migrates to the wider
  bound.
- The `Ratio` line in the profile report divides by the 262144 full pool, not
  the printed avg-total column — read `avgVisible` absolute counts, as this
  table does.

One task, one PR. — worker (fable planner)

---

## Outcome (recorded 2026-08-22)

PR #2475 implemented steps 1–3 and merged as `18e2fb58`. Realized gate numbers
(GL, Windows native / mingw64 / NVIDIA; dense frozen harness, sun shadows ON,
300 frames per config):

| zoom | visible pv-off → pv-on | feeder pv-off → pv-on (**ring capture**) |
|---|---|---|
| 4  | 255,275 → 29,589.3 (88%) | 4,108 → 1,136.0 (**72%**) |
| 8  | 167,426 → 16,240.3 (90%) | 47,995 → 8,595.2 (**82%**) |
| 16 | 67,567 → 8,028.8 (88%) | 83,922 → 12,135.9 (**86%**) |

Ring capture is non-zero at every zoom (the positive-fire gate) and the
shadows-ON md5 A/B stayed byte-identical on every cardinal shot (the identity
gate). Steps 4 and 5 — this file and the design-doc update — landed separately
in PR #3041. The Metal-side re-measurement on the post-#2898 baseline is
outstanding and is tracked as its own macOS-host issue.
