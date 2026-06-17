# Plan: render — unify depth/clipping across render types

- **Issue:** #1884
- **Model:** opus
- **Date:** 2026-06-16
- **Epic:** #1881 — see .fleet/plans/issue-1881.md
- **Blocked by:** #1883 (shared framebuffer gather/scatter surface — serialized)
- **Gated on:** PR #1880 (harness) on master

## Scope
Voxels disappear / occlude wrong due to depth-unit mismatch between render
types. Least-diagnosed — likely an investigation spike first.

## Affected files (audit start)
- engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp
- engine/render/src/shaders/f_trixel_to_framebuffer.glsl (+ metal) depth normalize
- engine/render/src/shaders/v_peraxis_scatter.glsl composite depth key
- engine/render/include/irreden/render/ir_render_types.hpp depth-encoding constants

## Approach
Audit + unify the framebuffer depth units across: per-axis scatter
(scatterCompositeDepthKey), single-canvas gather (pos3DtoDistance x4), detached
canvas (framebuffer-unit, #1872/#1624), floor. If not reproducible from one
audit, reframe as an investigation spike: produce a repro + a unit table before
prescribing the fix.

## Acceptance criteria
- Multi-render-type scene (voxel cube + detached canvas + floor) composites
  correct occlusion at all yaws — no vanishing, no wrong-order draw.
- A documented unit table of what each type writes to framebuffer depth.
- Both backends.

## Gotchas
Byte-identical fast paths (#1624 screenLocked_ overlay; cardinal-0) must stay
intact; depth-encoding constants are shared across all render types.

## Verification
Author a multi-type repro scene/shot; sweep + visual occlusion check on both
hosts.
