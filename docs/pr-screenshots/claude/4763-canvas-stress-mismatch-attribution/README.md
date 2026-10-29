# CanvasStress mode-switch, detach and impulse mismatches

All five inherited mismatches survive on master `469402034`, and one commit
causes all of them: #4615 `1a309a37d` ("sample cardinal shadows on continuous
receiver planes"). They are not reference drift. Every changed pixel is a
sun-visibility toggle on a GRID-resident (main-canvas, cardinal) voxel, and the
new shadow pixels are one-trixel islands on face boundaries. Unit-voxel casters
cannot project a shadow that small, so these islands are a real lighting defect.
It is a static defect, not a transient. It appears in fixed poses, and the live
mode-switch sequence returns byte-identically to its starting frame. No reference
was refreshed and no threshold changed.

Host: macOS arm64, Metal, `macos-debug` (Debug). Each capture is a fresh
`fleet-run` process with the manifest's argv ([`tools/cap.py`](tools/cap.py)).
Every run exited `RESULT=CLEAN`. [`results.json`](results.json) holds every
hash, verdict, comparator output and pixel class.

## Current master

`render-verify --target IRCanvasStress --no-build` on `469402034` fails 7 of 46
checks ([full log](master-render-verify.log.gz)):

| Row | Master | Match % | Max Δ | Changed px |
|---|---|---:|---:|---:|
| `modeswitch_grid` | FAIL | 99.9849 | 107 | 740 |
| `modeswitch_grid_return` | FAIL | 99.9849 | 107 | 740 |
| `detach_grid` | FAIL | 99.9891 | 110 | 536 |
| `impulse_midspin` | FAIL | 99.9588 | 96 | 2,024 |
| `impulse_settled` | FAIL | 99.9655 | 96 | 1,696 |
| `compare_yaw0`, `compare_yaw_q` | FAIL | 99.9992 | 240 | — (unrelated, below) |

None of the five has been fixed by a later merge. #4721, #4733, the glitch
presentation repair, #4777 and #4784 leave all five frames byte-identical to
`e9c594cd2`, the first revision measured red. The other 39 checks pass,
including all 16 cylinder receivers.

## Causal controls

| Build | Five rows vs reference | Five rows vs master |
|---|---|---|
| `378a00f7d` (first parent before #4615) | byte-identical | differ |
| `1a309a37d` (#4615) | differ | byte-identical |
| `e9c594cd2` | differ | byte-identical |
| master + [cardinal gate forced off](patches/control-cardinal-off.patch) | byte-identical | differ |

The window starts at #4590 (`c30f3ac35`), whose re-bless run passed all 46.
#4615 is one first-parent step after `378a00f7d`. The gate-off control changes a
single boolean, `cardinalSurfaceLightingEligible_`. On current master it
restores all nine frames of the three scenes exactly, so nothing else since the
references were blessed has moved these rows.

## Geometry, normals, depth ownership, shadow

Per-pixel classes, master vs reference ([`tools/pairs.py`](tools/pairs.py)):

- **Geometry and depth ownership unchanged.** No changed pixel involves the
  background. Object pixel counts are identical (10,880 / 125,128 / 164,480 /
  162,304). Every changed pixel keeps its albedo: 100% are a single scalar
  multiple of the reference colour. The pixel therefore still belongs to the
  same entity and face.
- **Normals unchanged.** Every lit value is one of the face's existing Lambert
  levels (e.g. 169/178/146 for the orange prism). Every dark value is the
  ambient-only level, 0.30 × albedo. Only sun visibility differs.
- **Only main-canvas GRID voxels change.** These are the orange GRID prism, the
  blue part detached to GRID, and the cyan GRID impulse solid. The detached
  re-voxelized solids, hosted parts and own-canvas parts are untouched, which
  matches #4615's eligibility predicate.
- **Shadow projection is wrong at face edges.** Sun elevation is about 37°, so
  a one-voxel step casts a band about 1.33 voxels long, or nothing. The new dark
  pixels are isolated 2×2 framebuffer blocks, one trixel each, sitting on the
  boundary between two faces of the same solid
  ([edge zoom](crops/impulse_settled_edge_zoom.png): reference | master |
  changed).

| Row | Edge speckles, reference | Edge speckles, master | Diagnostic clamp |
|---|---:|---:|---:|
| `modeswitch_grid` | 0 | 52 | 24 |
| `detach_grid` | 0 | 83 | 21 |
| `impulse_midspin` | 0 | 301 | 57 |
| `impulse_settled` | 0 | 268 | 10 |

Edge speckles are counted by
[`scripts/render-shadow-speckle-metric.py`](../../../../scripts/render-shadow-speckle-metric.py).
These are components of at most 4 px that are shadowed against a lit
same-albedo neighbour and whose ring touches two distinct lit face levels.
The passing control rows (`modeswitch_revox`, `detach_own_canvas`,
`sharedparts_spin_a`) read 0.

Crops: [mode switch](crops/modeswitch_grid.png),
[detach](crops/detach_grid.png), [impulse](crops/impulse_settled.png).
Full master frames are under [`master/`](master/). `modeswitch_grid_return`
hashes identically to `modeswitch_grid`.

### Mechanism (diagnostic, not a fix)

`cardinalVoxelSurfacePoint` intersects the fragment ray with the stored face
plane. By design it does not clamp the point to the owning micro-square. At a
face edge, rectangular storage assigns a fragment to a face whose plane
intersection falls outside that face. The point lands at or inside the
neighbouring voxel, and the finite shadow query reports it occluded.

[Clamping to the micro-square](patches/diag-clamp-micro-square.patch), Metal
only, removes 54–96% of the edge speckles. Some remain at every row, and all
five rows stay red. The clamp also contradicts the documented contract in
`docs/design/surface-shadow-sampling.md` ("Do not clamp this receiver point")
and its mutation test, so the fix is a design decision. It is escalated on the
PR rather than made here.

## Live sequences

All three manifest scenes are single live processes, not fixed-pose restarts.
The mode switch runs GRID → DETACHED → DETACHED_REVOXELIZE → GRID. The detach
scene moves parts from hosted to their own canvas to GRID. The impulse scene
goes from rest through a kick to spin and back to rest. Within the live
mode-switch run, `modeswitch_grid_return` is byte-identical to `modeswitch_grid`
on master, on #4615, and in the reference. A full round trip through two
detached routes leaves no residue, so the defect is the cardinal receiver
itself, not a transition. The DETACHED and REVOX frames in that sequence are
byte-identical to their references.

## Backends

- **Metal** is graded above.
- **Windows/OpenGL is not graded here.** `windows-debug` and `linux-debug` have
  no references for these rows, so a sweep on those hosts skips them. #4615
  mirrors the same unclamped reconstruction in
  `ir_cardinal_voxel_receiver.glsl`, so the defect is expected there too. The
  speckle metric needs no reference, so a Windows host can grade it from a
  capture of the same three scenes.

## Unrelated failure, reported separately

`compare_yaw0` and `compare_yaw_q` fail on master with max Δ 240 and 99.9992%
match. They passed in the #4721 broad run. Their cause is outside this
attribution.

## Replay

```sh
python3 docs/pr-screenshots/claude/4763-canvas-stress-mismatch-attribution/tools/cap.py \
  --build-dir build --out /tmp/canvas-rows --scene modeswitch   # also detach, impulse
python3 scripts/render-shadow-speckle-metric.py /tmp/canvas-rows/modeswitch/modeswitch_grid.png
python3 -m unittest scripts/tests/test_render_shadow_speckle_metric.py
```

The parent and #4615 controls were built in a detached scratch worktree with its
own build directory. Before each capture, every staged Metal shader was checked
with `cmp` against the checked-out source.
