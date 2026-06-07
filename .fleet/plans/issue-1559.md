# Plan: detached re-voxelize — cross-backend parity audit + render-verify (P5, repurposed)

- **Issue:** #1559
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `.fleet/plans/issue-1553.md`
- **Blocked by:** #1558

## Repurposed (architect)
The original "port the GPU scatter to Metal" is **obsolete** — every shader-adding phase (P2–P4) now ships **both** backends in-phase (the engine's Metal backend aborts on a missing `.metal`, so GLSL-only intermediates can't land on master; see the umbrella architecture decisions). There is no bulk Metal port left to do. This phase becomes the **final cross-backend correctness gate**.

## Scope
A final parity audit across the detached re-voxelize path on both backends, plus render-verify baselines.

## Approach
- Run the `backend-parity` skill across the re-voxelize shaders + render-impl wiring to catch any GLSL/Metal drift accumulated over P2–P4 (CPU↔GLSL↔Metal `roundHalfUp` parity, binding points, dispatch grids).
- Build + run `IRCanvasStress` on `macos-debug` (Metal) and a GL host; capture the re-voxelize shots on both.
- Commit render-verify reference images for the asymmetric + cube re-voxelize shots (the references P6 regression-checks against).

## Acceptance criteria
- OpenGL and Metal outputs match (full-frame + ROI) for the re-voxelize detached path — no drift.
- Render-verify baselines committed for both the asymmetric solid and the cube.

## Gotchas
- If a per-phase Metal mirror was skipped or drifted in P2–P4, this is where it surfaces — fix the offending phase's shader, don't paper over it here.

## Verification
`backend-parity` audit clean; `render-verify` green on both backends.
