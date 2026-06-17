# Plan: render — cardinal-gather coverage loss at non-start cardinals

- **Issue:** #1882
- **Model:** opus
- **Date:** 2026-06-16
- **Epic:** #1881 — see .fleet/plans/issue-1881.md for full context + baseline numbers
- **Gated on:** PR #1880 (harness) on master

## Scope
Fix the single-canvas cardinal path so a solid 64^3 cube is dense at every
cardinal (yaw 0, pi/2, pi, 3pi/2), not just the start one. Primary "density
goes away" defect. Per-axis path is already correct — do not touch it.

## Affected files
- engine/render/src/shaders/c_voxel_to_trixel_stage_1.glsl:271-294 (+ _stage_2 + both metal twins)
- engine/render/src/shaders/ir_iso_common.glsl ~L344-361 (de-tile), L423-446 (rotate+shift); metal/ir_iso_common.metal ~L327-360
- engine/render/src/shaders/metal/trixel_to_framebuffer.metal:93 ; f_trixel_to_framebuffer.glsl:53
- engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp:60-104 (cardinal fill)
- engine/render/include/irreden/render/ir_render_types.hpp:84-208 (struct + static_asserts) if plumbing a field

## Approach
Compensate the gather for the full per-cardinal cardinalLowerCornerShift iso
offset (k0:(0,0) k1:(-1,+1) k2:(0,+2) k3:(+1,+1)) — fold it into the gather's
canvasOffset/de-tile origin per cardinal so store + gather agree at every k.
The cardinal fill does not set visualYaw_/cardinalIndex on the gather frame
data today; set it and derive the offset (FrameDataTrixelToFramebuffer field
append, no new bind point; update CPU+GLSL+Metal struct + static_asserts in one
change). Reconcile the #394 Metal sawtooth so 0 stays byte-identical.
Alternative: reformulate cardinalLowerCornerShift to be iso offset/parity
neutral at all cardinals (then the fixed-origin gather is correct everywhere).

## Acceptance criteria
- scripts/dev/perf-grid-rotate-sweep build dense 60: all four cardinals
  (steps 0/9/18/27) coverage >= 0.99 AND perim within ~2x of the 0 baseline
  (~250); no regression at any per-axis pose or at 0.
- Holds on BOTH Metal (macOS) and OpenGL (Linux).
- Cardinal-0 fast path byte-identical to master.
- Before/after ramp screenshots at 0/90/180/270 in the PR.

## Gotchas
#394 Metal sawtooth must not return at 0; std140 silent-sync if plumbing (no new
bind point, budget full 0-30); per-axis path is forward-scatter (no gather) —
already correct, don't touch.

## Verification
On macOS: bash scripts/dev/perf-grid-rotate-sweep build dense 60 (check the
table). On Linux: fleet-build IRPerfGrid + the same sweep. Plus
fleet-build/run IRShapeDebug --auto-screenshot for the cross-host smoke.
