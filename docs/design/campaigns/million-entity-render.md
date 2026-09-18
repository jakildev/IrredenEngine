# Campaign: million-entity-render

Objective: [`objectives/million-entity-render.md`](../objectives/million-entity-render.md).
Protocol: [`docs/agents/campaign-protocol.md`](../../agents/campaign-protocol.md).
Seeded 2026-09-18 from the Codex session worklists
([`rendering-audit-todo.md`](../rendering-audit-todo.md),
[`docs/perf/rotation-subdivision-audit.md`](../../perf/rotation-subdivision-audit.md),
[`docs/perf/world-scale-visibility.md`](../../perf/world-scale-visibility.md));
those files stay the canonical detail. This file orders the directions,
records decisions and carries the slice in flight.

## Spirit

- Voxels are reconstructed as trixel faces on 2D canvases; rotation is a
  distortion of those canvases. No meshes, no sub-trixel geometry, and the
  distance/color/id trixel texture contract stays so lighting, shadow and
  AO stages keep working unchanged.
- Clean projected edges come from geometry. Blur, bias, inflated coverage
  or averaged normals are never a fix; raw rectangular trixel data is a
  debug view only, and every voxel path presents through the fragment
  reconstruction with one shared triangle-orientation convention.
- Lighting is geometric: what a face receives depends on what occludes it,
  itself or other objects, not on which iso face it is. AO is local ambient
  visibility; sun visibility and Lambert are separate terms.
- Cost tracks useful screen coverage. Rotation and zoom parity are targets;
  a fixed framebuffer does not bound intermediate work by itself, so reject
  work before generating it.
- Visibility is an unbounded orthographic prism; chunks are rejected by
  projected bounds under yaw; simulation cadence, shadow relevance and
  residency are separate decisions from render visibility.
- No claim without a control: full-frame GPU accounting, fingerprints,
  repeated runs, and a pixel-identity capture set per optimization PR. A
  small native scene never proves fleet-scale throughput.
- Both backends: GLSL and Metal twins land together; an OpenGL runtime gap
  is stated, not assumed away.

## Directions

Ordered; earlier directions gate later claims. A direction is done when
its objective Done-means rows verify on `origin/master`.

| # | Direction | Draws from | Exit |
|---|---|---|---|
| D0 | Close the remaining visual gates: mixed private SDF/voxel canvas density, placement, owner rotation and atomic-depth lifecycle (the shape pass clears the voxel pass's data); SDF BOX display extent versus receiver and analytic caster extent across subdivisions and yaw; SDF surface face ownership audit; revoxelized self-shadow/AO patches without flattening real staircase normals; deterministic face-connectivity and triangle-parity tooling; detached cardinal compensation (#3023, #3342) | `voxel-and-sdf-rendering.md` § Current acceptance work 2–5, `rendering-audit-todo.md` | new gates in `render-verify` / `scripts/render-*-metric.py`; nine-yaw CanvasStress and ShapeDebug probes pass |
| D1 | Honest million controls: committed `million` preset and `repeat_profile` recipe; Release and profiling-off arms; continuous yaw; independent entity motion; matched projected extent; tail latency and memory; a `--yaw` perf-matrix axis (#3130) | `world-scale-visibility.md` step 1, `rotation-subdivision-audit.md` 2, 7, 8 | one table under `docs/perf/` every later PR diffs against |
| D2 | Bound intermediate work: overflow sort at scale preserving `(cell, distance, color)` order (#3129, #3467); live-count clears and dispatches; per-axis storage and finalization dedup; scratch-byte and occupied-cell counters; paged versus larger pools decided from measured residency | `rotation-subdivision-audit.md` 1, 4, 8; `gpu-cost-attribution.md` next experiments | GPU frame time at the million control drops with pixel-identical captures |
| D3 | Hierarchical visibility: visited/admitted chunk counters; bound inflation of allocation-slot groups measured; spatial regrouping or a region index over occupied space; conservative light-directed caster region; unbounded depth and full-turn tests stay green | `world-scale-visibility.md` steps 2–3, `rotation-subdivision-audit.md` 3 | admitted candidates track projected coverage; the depth-cutoff suite passes |
| D4 | Lighting bounds: light-volume active tiles that keep off-screen lights and blockers; finite caster reach; AO cost by region; quality options only after dominant costs are known | `rotation-subdivision-audit.md` 5, `light-volume-candidate-pruning.md` | lighting cost bounded independently of entity count, lighting-demo captures identical |
| D5 | Simulation cadence: fixed-step catch-up amplification; staggered updates; presentation interpolation; `UpdateVoxelSetChildren` and transform propagation cost; bounded promotion under camera pans | `world-scale-visibility.md` step 6, `rotation-subdivision-audit.md` 9 | ≤ 1 update per rendered frame at the million control with correct motion |
| D6 | Cross-backend and gate: OpenGL runs of D0 gates and the million control; GL frame-query ring; the CI perf gate gating (#2817, #3471); Release baselines committed | `rotation-subdivision-audit.md` 7, objectives `backend-parity`, `machine-verified-behavior` | both backends measured, gate red on regression |
| D7 | Structural decision: LOD or voxel mip at zoom-out and the scatter-versus-gather call, made from D1–D4 data by the #1808 decision rule | #1808 D3, `lod-strategy.md` | a design doc with numbers, then its own campaign or epic |

D1 slices touch only tooling and docs, so they may interleave with D0.

## Working agreement

- One slice, one PR, one question. Measurement before mechanism.
- Fixtures: `IRPerfGrid --mode voxel_set --grid-size 100 --wave-freeze --zoom 4`
  with the pool-128 runtime config; the nine-yaw `IRCanvasStress` capture
  set; `IRShapeDebug` probes; the `lighting` demo for propagation changes.
- Every render PR: `attach-screenshots` stills, full frames plus crops, and
  the RGB-identity comparison of the nine CanvasStress poses. Every perf
  PR: `repeat_profile.py` tables with fingerprints, full-frame GPU
  accounting, and the direction it advances named in the body.
- Checkpoint every four to six open PRs; approve only after a fresh-context
  reviewer pass with findings addressed.
- Follow-ups outside the current direction are filed with
  `**Objective:** million-entity-render` and listed below.

## Ledger

| Date | Entry |
|---|---|
| 2026-09-14 | Stack merged: shadow reception, finite face casting, receiver alignment, rotation/subdivision audit (#3386–#3439) |
| 2026-09-15 | Stack merged: update spans, CPU stacks, GPU-write preservation, overflow dedup, depth ties (#3442–#3449) |
| 2026-09-17 | Stack merged: light-volume pruning, yaw visibility tests, million capacity, overflow demand sizing, live-count lighting and sort, GPU frame accounting, source-face presentation (#3450–#3477); geometric AO (#3484) |
| 2026-09-18 | Campaign seeded; the Codex session's in-flight item (mixed private canvas lifecycle) becomes D0's first slice |

### Decisions taken

- 2026-09-18: D0 stays ahead of D2–D5 (visual correctness before
  optimization, as the audit worklist orders it); D1 interleaves because
  it changes no render code.

### Follow-ups filed

- (none yet)

## Now

- **In flight:** D0.1 — mixed private SDF/voxel canvas lifecycle: reproduce
  the shape pass clearing voxel-pass data on a shared private canvas at
  high zoom, fix ownership so both producers' color, depth and entity id
  survive, and add the gate.
- **Next:** D1.1 — the committed `million` preset and `repeat_profile`
  recipe with Release and profiling-off arms.
