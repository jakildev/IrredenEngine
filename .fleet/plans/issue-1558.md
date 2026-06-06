# Plan: detached re-voxelize — AO / sun / light integration (P4)

- **Issue:** #1558
- **Model:** opus
- **Date:** 2026-06-06
- **Epic:** #1553 — see `~/.fleet/plans/issue-1553.md`
- **Blocked by:** #1557

## Scope
Light the re-voxelized detached canvas at parity with an attached GRID solid (AO + sun-shadow + light-volume + Lambert).

## Affected files
- Per-canvas lighting systems (AO / sun-shadow / lighting-to-trixel) — confirm they run over the re-voxelized detached pool
- `docs/design/per-axis-sun-shadow-resolve.md` if the detached lighting contract changes

## Approach
Per the epic plan §P4. The canvas uses cardinal normals (rotation is in cells), so the standard lighting path applies — do NOT port the per-axis residual-aware lighting (retired for detached in P6).

## Acceptance criteria
Lit equivalently to an attached GRID solid; shadows cast/receive per contract; identity/static byte-identical.

## Gotchas
Avoid pulling in the per-axis residual lighting machinery; that is the path being retired.

## Verification
`fleet-run IRCanvasStress --auto-screenshot`; compare lighting vs an attached GRID solid in the same scene.
