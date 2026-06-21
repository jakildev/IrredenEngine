# Plan: render — GPU depth-readback probe for composite depth-test debugging

- **Issue:** #1910
- **Model:** opus
- **Date:** 2026-06-19
- **Blocked by:** (none) — independent tooling; parallel with #1883 (named
  sibling, not a dependency)
- **Part of:** epic #1881 (tooling prerequisite for child #1884)
- **Unblocks:** #1884 (unify depth/clipping)

## Why (motivation)

#1884 has two confirmed "behind-face wins" composite-depth crossings:

- **A** — a floating detached cube (canvas_stress canary, world z=−8, *above*
  the floor z∈[2,6]) clips behind the SDF floor at high zoom.
- **B** — a per-axis cube shows a Y-over-X doubled face-stripe at cardinal-180
  (proven NOT a margin/yield issue — persists at yield scale 20).

Both need the **actual stored composite depth** at specific pixels, decoded per
render path, to root-cause. The prior session's shader-output approximation
(render depth as grayscale) gave **contradictory** data: it loses precision
(8-bit), can't be decoded per-path without knowing which path won the pixel, and
doesn't read the true depth-test value across the entity-composite vs
main-canvas passes. We need a trustworthy **GPU→CPU readback**, not another
shader-output approximation or parameter sweep. Reusable for any future
depth-ordering bug.

## Scope

A debug path that, for a requested screen pixel (or small set), reads and logs
the **real** depth-test value(s) in a reconciled, comparable form:

1. **Framebuffer depth attachment** at the pixel — the final composite winner's
   `gl_FragDepth`. Decode to raw distance via the exact inverse of
   `normalizeDistance` (`dist = norm·(kMax−kMin) + kMin`, same
   `kTrixelDistanceMin/MaxDistance`).
2. (stretch) **Per-contributing-canvas distance texture** at the corresponding
   texel, each decoded by ITS path's encoding so the same world point can be
   compared across paths:
   - voxel grid / SDF floor: `encodeDepthWithFace` / `baseDepth·4` →
     `rawDepth = dist>>2`.
   - per-axis scatter: `#1458` `(depth<<10)|(uFrac<<6)|(vFrac<<2)|slot` →
     `rawDepth = dist>>10`.
   - detached canvas: `round(rawDist·depthScale) + distanceOffset` (the #1624
     lift; `depthScale = effSub/cubeSub`).

Output: `IR_LOG_INFO` lines of `pixel=(x,y) path=<...> normDepth=… rawDist=…
[worldDepth=…]` so a human/script can read the ordering directly.

## Approach

- Add a debug-gated readback after the framebuffer composite:
  - **OpenGL:** `glReadPixels` on the depth attachment (+ the distance-texture
    texel via `glGetTexImage` / FBO read).
  - **Metal:** `getBytes` with an **explicit commit+wait** (picking already does
    this — #1436; reading mid-frame without it returns garbage). For per-canvas
    distance, do NOT bind a foreign canvas's R32I as a compute read input
    (#1640 returns the clear value) — read the post-composite attachment, or
    resolve-then-read.
- Expose via `IRRender::readbackCompositeDepth(ivec2 px)` (+ a `DebugOverlayMode`
  or a plain debug call) and a CLI flag in the demos: `--depth-probe X,Y`
  (canvas_stress + perf_grid) that logs the readback for that pixel each frame.
- Debug-gated: zero effect (byte-identical fast path) when the probe is off.

## Affected files (start)

- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` —
  post-composite readback.
- `engine/render/include/irreden/ir_render.hpp` —
  `IRRender::readbackCompositeDepth`.
- `engine/render/.../ir_render_enums.hpp` — `DebugOverlayMode` if used.
- `f_trixel_to_framebuffer.glsl::normalizeDistance` — the decode inverse
  (read-only reference).
- `creations/demos/canvas_stress/main.cpp` +
  `creations/demos/perf_grid/main.cpp` — `--depth-probe X,Y`.

## Acceptance criteria

- [ ] A debug path logs the **real** composite depth-test value at a named pixel
  (decoded rawDist + normDepth), reconciled so the same world point reads the
  same value regardless of which render path produced it.
- [ ] **Sanity-validates** on a known-correct case: a voxel cube sitting on the
  floor reads cube-depth < floor-depth at pixels where the cube occludes the
  floor.
- [ ] **Reproduces the #1884 bugs with trustworthy numbers**: (A) the detached
  canary's stored depth at the clip pixels vs the floor's; (B) the per-axis
  Y-face vs X-face depths at the cardinal-180 stripe pixels — each showing the
  wrong ordering in real units (this is the artifact that makes #1884 tractable).
- [ ] Works on **both** backends (OpenGL `glReadPixels` + Metal `getBytes`
  commit+wait).
- [ ] No render-path change when the probe is off; cardinal/static fast paths
  byte-identical.

## Gotchas

- **Metal deferred readback** needs explicit commit+wait (#1436) — mid-frame
  depth reads are garbage otherwise.
- **Foreign-canvas R32I** as a compute read input returns the clear value on
  Metal (#1640) — read the composite attachment or resolve-then-read; never bind
  a non-main canvas's model-frame distance texture as a bake/compute read.
- Decode must use the **exact** `normalizeDistance` inverse with the SAME
  `kTrixelDistanceMin/MaxDistance`, or the comparison is meaningless.
- A readback stalls the GPU pipeline — keep it strictly debug-gated and ideally
  single-pixel.

## References

#1884 (the two depth crossings this unblocks) + its findings comment; #1436
(Metal readback commit+wait discipline); #1640 (foreign R32I read restriction,
resolve-then-read); `normalizeDistance` in `f_trixel_to_framebuffer.glsl`;
`scatterCompositeDepthKey` (per-axis) + `encodeDepthWithFaceFrac` (#1458) in
`ir_iso_common.{glsl,metal}`; #1624 world-placed detached depth
(`system_entity_canvas_to_framebuffer.hpp`). Epic #1881.
