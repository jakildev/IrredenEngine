# Plan: detached re-voxelize — Metal parity (P5)

- **Issue:** #1559
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `~/.fleet/plans/issue-1553.md`
- **Blocked by:** #1558

## Scope
Port the GPU scatter compute (+ any GLSL changes from P1–P4) to Metal; build + run on macOS/Metal.

## Affected files
- `.metal` mirrors of the scatter compute + any modified raster/lighting shaders in `engine/render/src/shaders/`

## Approach
Per the epic plan §P5. Mirror the shaders; keep CPU↔GPU transform math bit-identical; build `macos-debug`; run the re-voxelize demo. Use the `backend-parity` skill.

## Acceptance criteria
Metal matches OpenGL (full-frame + ROI) for the re-voxelize detached path; parity verified.

## Gotchas
Metal has no free buffer index past 30 — keep the transform-slot bit-packed in the local-position `.w` lane (#1396 convention).

## Verification
`cmake --preset macos-debug`; build + run `IRCanvasStress`; side-by-side vs OpenGL captures.
