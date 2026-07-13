# Plan: render — rotated-voxel correctness under camera Z-yaw (epic)

- **Issue:** #1881 (umbrella, `fleet:epic`)
- **Model:** opus (all children)
- **Date:** 2026-06-16

## Scope

A solid voxel cube must read as a solid cube at every camera Z-yaw. Today it
does so only at the start cardinal (yaw 0). Three distinct defects in the
rotated-voxel render path are diagnosed below. PR #1880 shipped the validation
substrate that localizes and gates them; every child validates against it.

## Validation substrate (PR #1880 — must be on master first)

- `IRPerfGrid --mode dense --grid-size 64 --yaw-ramp --auto-screenshot <warmup>`
  rotates a solid 64^3 cube slowly through 0..2pi in 36 fixed 10deg steps at a
  fixed whole-cube zoom. The slow ramp keeps the iterative lighting (light
  volume / AO / sun-shadow) converged frame-to-frame, so the metrics reflect
  geometry/coverage, not a settling artifact from big discontinuous jumps.
- `scripts/dev/perf-grid-rotate-sweep [build_dir] [mode] [warmup]` drives
  build -> ramp -> per-frame score and prints step/yaw_deg/coverage/hole_ratio/
  perim_ratio + worst-pose summary. Metrics: render-coverage-metric.py
  (interior-hole ratio) and render-silhouette-metric.py (perimeter^2/area; a
  clean iso cube ~250, a sparse stipple/shatter spikes to 100k+).

Baseline (origin/master, Metal): cardinal 0 -> coverage 0.994 / perim 242;
per-axis poses -> 0.95-0.99 / perim 50-2000; cardinals pi/2, pi, 3pi/2 ->
coverage 0.61-0.71 / perim ~140000 (sparse see-through mesh).

## Backend note (all children)

macOS renders the Metal shaders (engine/render/src/shaders/metal/); Linux/
Windows render the GLSL (engine/render/src/shaders/). Every shader fix lands on
BOTH backends and is smoke-validated on both (fleet:needs-macos-smoke /
fleet:needs-linux-smoke). Child 1 was confirmed on Metal; OpenGL likely shares
it (de-tile is cardinal-blind on both) but verify.

---

## Child 1 [opus, primary] — Cardinal-gather coverage loss at non-start cardinals

SYMPTOM: dense 64^3 cube is solid at yaw 0 but a sparse see-through dot-mesh at
exact cardinals pi/2, pi, 3pi/2 (coverage 0.61-0.71; holes are 100% pure
(0,0,0) = true background, NOT dark/unlit faces; perim explodes ~600x). The
per-axis residual path renders the identical geometry fine, so geometry + voxel
data are intact; only the single-canvas cardinal path (residualYaw==0,
smoothYaw_ off) is broken.

RULED OUT (by reading real shaders + instrumentation): old -X/-Y/-Z face model
(cardinal store uses visibleFaceIds[slot] x faceIsExposed, #1278,
c_voxel_to_trixel_stage_1.glsl:166-193); simple parity flip (pos3DtoPos2DIso of
an integer is always even; rotateCardinalZ + cardinalLowerCornerShift preserve
it); lighting (holes are pure black).

ROOT CAUSE: the store applies cardinalLowerCornerShift(cardinalIndex)
(ir_iso_common.{glsl,metal} ~L441) before pos3DtoPos2DIso. Its iso projection
is per-cardinal: k0:(0,0) k1:(-1,+1) k2:(0,+2) k3:(+1,+1). The framebuffer
gather's canvas->screen mapping (canvasOffset / trixelOriginModifier /
trixelFramebufferSamplePosition) does NOT compensate that per-cardinal offset,
so store + gather agree only at k0. On Metal additionally,
trixel_to_framebuffer.metal:93 reads color/distance at the RAW origin (the
de-tile row-shift is disabled there per #394 -- it caused a 1px sawtooth),
correct only when the shift's iso-y parity is even. A/B (force the Metal
de-tile) recovers 90/270 (0.69->0.94, 0.61->0.92) but 180 stays broken in both
modes (raw 0.71, de-tile 0.67) and 0 regains the #394 sawtooth -- the de-tile
only corrects a +-1 row, not k2's +2-cell offset. So this is a
cardinalLowerCornerShift <-> gather DESYNC, not a parity bit.

DIRECTION (recommended: store-side reformulation / gather compensation):
- Preferred: compensate the gather for the full per-cardinal
  cardinalLowerCornerShift iso offset -- fold its iso projection into the
  gather's canvasOffset (or de-tile origin) per cardinal so the gather samples
  the cell the store wrote at every k. Needs cardinalIndex (or the precomputed
  iso offset) in the gather; the cardinal fill
  (system_trixel_to_framebuffer.hpp:60-104) does NOT currently set
  visualYaw_/cardinalIndex on the gather frame data -- set it and derive the
  offset. (FrameDataTrixelToFramebuffer field append only -- no new bind point;
  update CPU + GLSL + Metal struct + static_asserts together -- silent-sync
  risk.)
- Alternative: reformulate cardinalLowerCornerShift so its iso projection is
  offset/parity-neutral at all cardinals while still aligning the 2x3 diamond
  emit with the voxel footprint; then the fixed-origin gather is correct
  everywhere.
- Reconcile the #394 Metal sawtooth so 0 stays byte-identical.

FILES: c_voxel_to_trixel_stage_1.glsl:271-294 (+ _stage_2 + both metal twins);
ir_iso_common.glsl ~L344-361 (de-tile) / L423-446 (rotate+shift) + metal/
ir_iso_common.metal ~L327-360; metal/trixel_to_framebuffer.metal:93 +
f_trixel_to_framebuffer.glsl:53; system_trixel_to_framebuffer.hpp:60-104;
ir_render_types.hpp:84-208 (FrameDataTrixelToFramebuffer + static_asserts) if
plumbing a field.

ACCEPTANCE: sweep shows all four cardinals (steps 0/9/18/27) at coverage >= 0.99
AND perim within ~2x of the 0 baseline (~250); no regression at any per-axis
pose or at 0; holds on BOTH Metal and OpenGL; cardinal-0 fast path
byte-identical (no sawtooth); before/after ramp screenshots at 0/90/180/270.

GOTCHAS: #394 Metal sawtooth must not return at 0; std140 silent-sync if
plumbing (update CPU+GLSL+Metal+asserts in one change, no new bind point,
budget full 0-30); per-axis path is unaffected (forward-scatter, no gather) --
do not touch it.

---

## Child 2 [opus] — Per-axis face seams / slivers / stray lines

SYMPTOM: zoomed small rotated cubes (canvas_stress 48-55) show thin sliver/gap
bands at top<->side edges, horizontal region offsets within a face, stray thin
diagonal/vertical bright lines, staircase edges; worst near a face-flip. This is
the per-axis forward-scatter path (residual yaw).

ROOT CAUSE: #1878 adaptive dilation (v_peraxis_scatter.glsl:199-222) is
anisotropic (single scalar max(suLen,svLen) mitered across both edges) and
discontinuous (hard degenSin gate swings 0.85px <-> full voxel step) -> stray
lines; at close zoom every quad is in the grow arm (its own comment at
ir_iso_common.glsl:716 says the camera path "isn't the small-cube regime this
addresses"). Float-ulp split: scatter consumes isoPixelToPos3D un-rounded
(v_peraxis_scatter.glsl:165) while resolve/lighting consume it rounded -> region
offset. Cross-canvas shared edges have no geometric owner (depth + 0.25 bias
only).

DIRECTION: make the dilation margin per-axis (grow each in-plane axis by its own
collapse, not the max) and continuous (drop the hard degenSin gate); unify the
rounding (consume isoPixelToPos3D the same way in scatter + resolve/lighting);
give the cross-canvas shared edge a deterministic owner (stable axis/slot tie
rule) instead of an ulp-sensitive depth race.

FILES: v_peraxis_scatter.glsl:165,199-222 + metal/peraxis_scatter.metal;
ir_iso_common.glsl scatterConservativeDilation ~L752-777 (+ metal);
c_resolve_per_axis_screen_depth.glsl (+ metal) rounding parity.

ACCEPTANCE: add a zoomed shot tier (zoom 3-4) to the harness; at the per-axis
poses the zoomed perim drops near the cardinal-0 level and ROI crops show no
slivers / stray lines / region offsets; canvas_stress small cube reads clean in
the residual band; both backends.

GOTCHAS: margin-depth bias must still keep grown margin yielding to a real exact
footprint (don't bloat silhouette); don't regress the band-clipping #1878 fixed
at large residual (dense rotating solids must still fill).

---

## Child 3 [opus] — Depth / clipping unification across render types

SYMPTOM: voxels disappear or appear behind things; overall canvas-bounds +
distance mismatch between render types. Least-diagnosed of the three.

DIRECTION: audit the depth ranges/units each render type writes to the shared
framebuffer depth and unify them so cross-type compositing occludes correctly:
per-axis scatter (scatterCompositeDepthKey yawed iso depth); single-canvas
gather (pos3DtoDistance, encodeDepthWithFace x4); detached canvas
(framebuffer-unit depth, #1872/#1624 world-placed); floor/other canvases. If
not reproducible from a single audit, reframe as an investigation spike (the
literal phrase in the issue) and produce a repro + unit table before
prescribing the fix.

FILES (audit start): system_entity_canvas_to_framebuffer.hpp;
f_trixel_to_framebuffer.glsl (+ metal) depth normalize; v_peraxis_scatter.glsl
composite depth key; ir_render_types.hpp depth encoding constants.

ACCEPTANCE: a multi-render-type scene (voxel cube + detached canvas + floor)
composites correct occlusion at all yaws -- no voxel vanishing, no wrong-order
draw; a documented unit table of what each type writes to framebuffer depth;
both backends.

---

## Dependency chain

PR #1880 (harness, merged)
  -> Child 1 (#1882: HARNESS fix — isolate cardinals + zoom tier + near-cardinal sampling)
  -> Child 2 (#1883: per-axis scatter defect — coverage bands + face-alignment seams)
  -> Child 3 (#1884: depth/clipping unification)

Serialized: they share ir_iso_common + the framebuffer gather/scatter, so they are
worked one-at-a-time to avoid parallel conflicts. Child 1 (the harness) first — it
must distinguish the single-canvas vs per-axis render paths and surface the fine
artifacts before Child 2's per-axis fix can be validated.

**Re-scope (2026-06-17).** Child 1 was "fix the cardinal gather" until the #1885
investigation proved the single-canvas cardinal path is correct and the real defect
is the **per-axis** path (now Child 2 / #1883, which absorbed the coverage "bands").
Child 1 became the harness-methodology fix the misdiagnosis exposed. See the
per-ticket plans + the #1881 / #1885 comments.

## Steward ledger

reconciled-through: 2026-07-13 (all three direct children closed COMPLETED; checklist healed on first steward claim)
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #1882 | closed COMPLETED — cardinal coverage loss fixed (`Camera::computeYawSplit` deadband + render-path-aware harness) | #1885 | .fleet/plans/issue-1882.md | 2026-06-19 (Metal: 4 cardinals path=single, coverage 1.0) |
| #1883 | closed COMPLETED — per-axis bands + face-alignment seams; corner-spike residual forked to #1933 | #1907 | .fleet/plans/issue-1883.md | 2026-06-21 |
| #1884 | closed COMPLETED — depth/clipping unified across render types; perf-cliff forked to #1961/#1963 (+ #1983) | — | .fleet/plans/issue-1884.md | 2026-06-24 |

### Decisions
- 2026-06-17: #1882's original "single-canvas cardinal gather" premise was a
  misdiagnosis (single-canvas path verified correct via static cardinals + an
  early-return probe in #1885). Re-scoped #1882 → harness methodology fix; folded
  the real coverage loss + the face-alignment seams into #1883 (per-axis path).
- 2026-06-18: #1882 re-scoped FINAL back to the real fix — the cardinal coverage
  loss at 90°/180° is a residual-yaw gate mismatch, fixed via a single-source
  deadband in `Camera::computeYawSplit` (the interim "harness-only" re-scope was a
  degrees-vs-radians test bug). Delivered in #1885. Source: #1882 architect comment
  2026-06-18.
- 2026-07-13 (D-fork): residual render-quality beyond the three original defects
  forked into separate OPEN epics rather than reopening/broadening #1881 —
  corner-spike raggedness → #1933 (conservative rasterization); per-axis
  view-visible face drops under residual yaw → #2331 (view-visibility overflow);
  rotation perf cliff → #1961/#1963 (+ #1983). #1881's three direct defects are each
  resolved, but its WHOLE-EPIC acceptance sweep (`perf-grid-rotate-sweep dense 60`
  on BOTH Metal + OpenGL) is not steward-verifiable (steward writes docs only;
  macOS host cannot build the GL backend). Source: closing comments on #1883
  (PR #1907 → corner-spike follow-on) and #1884 (perf-track-refined comment
  2026-06-22).

### Events
- 2026-06-16: filed via file-epic; plans committed to repo retroactively (the stale global ~/.claude/skills/file-epic ran and skipped step 6.5).
- 2026-06-17: #1885 worker design-blocked child 1; architect accepted the finding, re-scoped #1882 (harness) + broadened #1883 (per-axis bands + alignment, with human screenshots), unblocked #1885; synced the per-ticket plans to match.
- 2026-07-13: steward first-claim heal — umbrella carried NO `## Children`
  checklist (unmanaged since filing). Built it from the verified direct members
  #1882/#1883/#1884 (all closed COMPLETED). Rejected free-text-search false
  positives that merely mention #1881: #1923/#1954/#1922/#1920/#1921/#1910/#2010
  (no `Part of epic:` line), #1937/#1939 (→ #1933), #2332 (→ #2331), #2323 (→ #2314),
  #2083 (→ #1717). Posted a close-out-readiness assessment on the umbrella; NOT
  closing this iteration — the whole-epic acceptance sweep is unrun and residual
  quality lives in open follow-on epics #1933/#2331. Close-out is the human's call
  (run the both-backend sweep and close, or close-as-forked).
