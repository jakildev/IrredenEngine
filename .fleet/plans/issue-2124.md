# Epic #2124 — Fog-of-war filled cross-section on voxel geometry (two modes)

- **Repo:** jakildev/IrredenEngine
- **Umbrella:** #2124 (`fleet:epic`)
- **Model:** opus (render + shader architecture)
- **Supersedes:** the deferred filled-cross-section follow-on of #2102; **#2106 held** (absorbed by P1)
- **Builds on:** #2055 (cull respects vision circles), #2106's shared `fogVisionCircleReveal` curve

## Goal
At the fog vision boundary, voxel **objects** reveal with a **filled cross-section** — you see the object's interior cut surface, not missing faces / see-through holes — in two modes, on every canvas that carries fog.

## Core mechanism — "cut faces" (no new geometry)
A voxel side face is emitted today only when `faceIsExposed(flagsByte, faceId)` (neighbor cell empty) — `c_voxel_to_trixel_stage_1.glsl:193`, same gate in stage_2 + Metal. The cross-section is the **interior face exposed by fog**:

> Emit a camera-visible **vertical** face (faceId in {X_NEG,X_POS,Y_NEG,Y_POS}) when it is NOT normally exposed (solid neighbor) BUT the neighbor **column** is fog-hidden.

The vision region is a vertical cylinder, so only +/-X / +/-Y faces are ever cut; +/-Z (top/bottom) never are. The neighbor column for a faceId is `voxelPosRaw.xy + faceNormalXY(faceId)`; "fog-hidden" reuses #2106's `fogVoxelColumnHidden` test (grid cell unexplored AND outside every live vision circle). Cost is bounded: only the <=2 vertical faces in a voxel's camera-visible triplet ever take the extra neighbor fog lookup, and only when `visionCircleCount > 0`.

This composes with #2106's existing per-voxel column **drop** (hidden columns' own voxels don't rasterize): the drop removes the hidden half, the cut faces cap the revealed half -> a filled slice.

## Two modes — the active reveal path IS the mode (no new toggle)
- **Mode A (voxel-resolution):** grid reveal (`revealRadius`, integer cells). Boundary voxel-blocky; objects reveal a column at a time; cut = voxel-aligned interior faces.
- **Mode B (smooth):** analytic reveal (`setVisionCircle`). Same cut faces, trimmed per-pixel by the existing `FOG_TO_TRIXEL` analytic mask so faces *and* cut walls follow the smooth disc.

## Cut shading = option 1 (settled)
Cut faces shade exactly like a normal voxel face — full AO / sun-shadow / light-volume, no special tint. They are real geometry written to the distance + color canvases, so lighting is automatic. (AO is screen-space neighbor sampling now (T-091); the hidden neighbor isn't rasterized, so the cut wall reads as an exposed wall — the intended look.)

## Phases
| Phase | Title | Model | Blocked by |
|---|---|---|---|
| P1 | Mode A — voxel-resolution cross-section on the GRID canvas (cut faces; absorbs #2106) | opus | (none) |
| P2 | Mode B — smooth cross-section on the GRID canvas (analytic clip of cut walls) | opus | P1 |
| P3 | Detached / re-voxelize canvas cross-section | opus | P1 |
| P4 | Per-axis rotation canvas cross-section | opus | P1 |

### P1 — Mode A (GRID, voxel-resolution) — head ticket
- **Scope:** cut-face mechanism in `c_voxel_to_trixel_stage_1` + `_stage_2` (GL + Metal); reuse #2106's `fogVoxelColumnHidden` + the binding-0 fog image + binding-27 observers UBO already wired into STAGE_1 by #2055/#2106. Add `faceNormalXY(faceId)` (or reuse an existing face-normal helper) + the cut-face gate. Absorbs #2106's per-voxel column clip (close #2106). GRID canvas only (gate on `perAxisRoute == 0 && isDetachedCanvas < 0.5`). Add a fog_demo cross-section scene (a tall voxel object straddling the boundary) + commit render-verify refs.
- **Acceptance:** a voxel object the boundary cuts shows a filled interior cut wall (no holes, no black wedge) under the grid reveal; away from the boundary byte-identical; GL + Metal; fog_demo refs committed.

### P2 — Mode B (GRID, smooth) — blocked by P1
- **Scope:** verify/extend so cut faces emit under the analytic reveal and the existing per-pixel `FOG_TO_TRIXEL` mask trims the cut walls to the smooth disc (faces + cut walls one analytic curve via shared `fogVisionCircleReveal`). Reconcile the cut-face neighbor-hidden test (binary, `aa=0`) with the per-pixel smooth edge so the cut wall is trimmed, not popped. fog_demo smooth-mode refs.
- **Acceptance:** under `setVisionCircle`, the object silhouette AND the cut wall follow the smooth disc edge — no per-column stair-step at the cut, no holes; GL + Metal.

### P3 — Detached / re-voxelize canvas cross-section — blocked by P1
- **Scope:** the detached/re-voxelize raster carries NO fog today (count-0 placeholder, `isDetachedCanvas > 0.5`). Propagate the world fog texture + observers into the detached STAGE_1 dispatch, recover the entity's WORLD column in the detached model frame (the re-voxelize path bakes rotation into cells; world cell = model cell + `worldCellOffset_`), and emit cut faces there. Reconcile with the world-placed depth/lighting path (#1624) so a detached object at the boundary cross-sections in world space.
- **Acceptance:** a rotating/detached voxel object straddling the boundary shows the same filled cross-section as a GRID object; non-fog detached scenes byte-identical.

### P4 — Per-axis rotation canvas cross-section — blocked by P1
- **Scope:** thread cut-face emission through the per-axis route (`perAxisRoute 1/2/3`, `c_voxel_to_trixel_stage_1.glsl:229`). Each axis canvas emits only its axis's faces; the cut face must route to the correct axis canvas and survive the forward-scatter composite. Reconcile with the base-resolution per-axis store + the scatter recovery.
- **Acceptance:** under continuous camera yaw, a voxel object at the boundary keeps its filled cross-section (no flicker/holes through the rotation); cardinal poses byte-identical.

## Dependency chain
```
P1 (head) -> P2 (smooth)
          -> P3 (detached)
          -> P4 (per-axis)
```
P2/P3/P4 are mutually independent; each only needs P1's cut-face mechanism landed.

## Verification (all phases)
`render-debug-loop` + ROI crops + cross-host smoke labels (GLSL+Metal). fog_demo gains a cross-section scene; render-verify refs are the regression guard. Default-off discipline: the cut faces only fire when `visionCircleCount > 0` / a fog canvas is bound, so non-fog scenes stay byte-identical — but each phase needs a positive ENABLED-path shot, not just default byte-identity.

## Steward ledger

reconciled-through: 2026-06-29
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2125 | open | — | plan | 2026-06-29 |
| #2126 | open | — | plan | 2026-06-29 |
| #2127 | open | — | plan | 2026-06-29 |
| #2128 | open | — | plan | 2026-06-29 |

### Decisions
- Cut shading = option 1 (lit-as-normal). Two modes = the existing grid/analytic reveal paths (no new toggle). #2106 held + absorbed by P1.

### Events
- 2026-06-29: filed via file-epic
