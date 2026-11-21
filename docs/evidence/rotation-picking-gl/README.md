# Rotation picking: OpenGL host reading

This is the OpenGL runtime verification #4865 asks for, for the rotation
picking that #4864 implemented and #4894 accepted on Metal.

- **Code under test:** master `eea79792d`. The run tree is the #4865 stack
  head `44b781f71` (PR #5217 over its macOS reference parent).
  `git diff --stat eea79792d 44b781f71`, with
  `creations/demos/shape_debug/test/references/macos-debug` and `docs`
  excluded, is empty. So every binary here is a master build, and the head
  runs double as the same-session master control.
- **Control arm:** `c614ba3d6`, the parent of #5115's merge `683701acd`
  ("gate mouse picks on completed frames"). It was built in its own worktree,
  and it separates what #5115 changed on OpenGL from what was already there.
- **Host:** Windows 10 Pro 10.0.19045, MSYS2 mingw64, `windows-debug` (Debug).
- **GPU:** NVIDIA GeForce RTX 3060 Ti, driver 32.0.15.8180, OpenGL.
- **Date:** 2026-11-19.

Every figure below is in [results.json](results.json): each arm's exact
command, its exit code and its verdict lines (`ROTATION-PICK`, failing
`GUI-ASSERT` lines, the `gui-verify` / gtest / `render-verify` summaries and
`ir-run: RESULT=`). The drivers are kept as text: [run.py](run.py.txt) runs
one arm through msys64 bash and `fleet-run` from the worktree;
[run_parent.py](run_parent.py.txt) runs the same arms against the control
build; [build_parent.py](build_parent.py.txt) built it; and
[collect.py](collect.py.txt) wrote `results.json`. Their absolute scratch paths
describe the executed runs; replace them for a replay. Every launch went
through `fleet-run`, so every run held the gpu lock.

## Summary

| Criterion | Reading | Verdict |
|---|---|---|
| Build ShapeDebug, CanvasStress, VoxelEditor, IrredenEngineTest | `fleet-build` of all four exited 0 | met |
| Native suite incl. OverflowSort and framebuffer entity-ID coverage, clean exit | 4,142 passed, 24 skipped, 3 failed (the three #5180 host failures); exit 1 | not met: #5180 |
| Rotation GUI 8/8 | 8/8 | met |
| Sweep 360/360 | 360/360 | met |
| Cube 288/288 | 140/288 and 141/288 | **not met: #5219** |
| Tie ×3: A and Bmin at every yaw, only {0, A, Bmin}, identical tallies | three identical PASS runs | met |
| ShapeDebug default GUI session | 48/49, three runs | **not met: #5220** |
| ShapeDebug continuous-zoom GUI session | 5/5 | met |
| CanvasStress `hover_canary` | 1/1 | met |
| VoxelEditor default session | 51/51 | met |
| VoxelEditor `part_gizmo_reach` | 14/24, two runs | **not met: #5221** |
| render-verify ShapeDebug, no re-bless | all 103 checks PASS, 83 skipped (no `windows-debug` reference) | met |
| render-verify CanvasStress, no re-bless | all 31 checks PASS, 28 skipped (no `windows-debug` reference) | met |
| `light-verify --if-changed` | 0 of 34 changed paths in the trigger surface; not required | met |

## Native suite

`fleet-run --timeout 3000 IrredenEngineTest`: `[  PASSED  ] 4142 tests`,
24 skipped by their own host guards, and three
failures: `ToolSpawnTest.UsesRootAsCwdAndSuppliesBuildDirectory`,
`ToolSpawnTest.SelectedWorktreeIsTheBuildCwdAndBuildDirectory` and
`FontMakerSource.SavedHeadersCompile`. Those are exactly the three that #5180
records on this host (open). The picking coverage the criterion names ran and
passed:

- `PositionUploadTest.OverflowSortHandlesFirstPopulationAndCountTransitions`
- `OverflowSortBoundTest.UsesAdmittedVoxelSlotsAndClampsToLiveCountAndCapacity`
- `PositionUploadTest.FramebufferEntityIdsAllocateLazilyAndClearIndependently`
- `PresentedPick.*` (3) and `GpuReadbackRing.*` (5)

## Rotation probes

| Arm | Command | Head `eea79792d` | Control `c614ba3d6` |
|---|---|---|---|
| GUI | `python3 scripts/gui-verify.py IRShapeDebug --no-build --timeout 600 -- --rotation-pick-test gui` | 8/8, each shot `hovered=457,457,457` | not run |
| Sweep | `fleet-run --timeout 180 IRShapeDebug --rotation-pick-test sweep` | `frames=360 hits=360 misses=0 result=PASS` | `hits=357 misses=3 result=FAIL` |
| Cube | `fleet-run --timeout 180 IRShapeDebug --rotation-pick-test cube` | `hits=140 misses=148`, then `hits=141 misses=147`; FAIL, exit 2 | `hits=135 misses=153 result=FAIL` |
| Tie ×3 | `fleet-run --timeout 180 IRShapeDebug --rotation-pick-test tie` | `frames=15372 hits=15372 misses=0 result=PASS` all three | not run |

The tie tallies (nearer / farther / background) were identical in all three
runs:

| Yaw | Nearer (A) | Farther (Bmin) | Background |
|---:|---:|---:|---:|
| 20 | 2018 | 1259 | 566 |
| 70 | 1908 | 1288 | 647 |
| 160 | 1900 | 1289 | 654 |
| 250 | 1894 | 1254 | 695 |

Both owners appear at every yaw and there are no misses, so no read fell
outside {0, A, Bmin}. The criterion grades repeatability on one host. These
tallies differ from Metal's (#4894: 2019/1259/565 at 20°, 1951/1254/638 at
250°; 70° and 160° match), and this reading does not explain the difference.

**The cube failure (#5219)** is the probe's timing, not ownership. Every miss
reads the entity expected for the sample before it
(`sample=1 expected=457 actual=456`, …, sample 0 reads background). The probe
grades a pick one render after it wrote the sample's cursor, but OpenGL keeps
submitted frames in flight. Under #5115's pick frame contract, one render
later is not completion evidence. The 144 twin-cube samples share one entity,
so only the four yaw-group boundaries miss there: 144 − 4 = 140. Before #5115
the probe read the live mapped buffer. The lag was then irregular, and it also
cost the sweep three samples.

## GUI sessions

| Session | Command | Head | Control |
|---|---|---|---|
| ShapeDebug default | `python3 scripts/gui-verify.py IRShapeDebug --no-build --timeout 600 -- --gui-test` | 48/49, three runs | 45/45 |
| ShapeDebug continuous zoom | `… IRShapeDebug … -- --continuous-zoom-gui-test` | 5/5 | not run |
| CanvasStress | `… IRCanvasStress … -- --gui-session hover_canary` | 1/1 | not run |
| VoxelEditor default | `… IRVoxelEditor --no-build --timeout 600` | 51/51 | not run |
| VoxelEditor gizmo reach | `… IRVoxelEditor … -- --gui-session part_gizmo_reach` | 14/24, two runs | 14/24, two runs |

- **ShapeDebug default (#5220).** The one failure is #5115's own
  `presented_pick_current_30_frames` / `newest_completed_pick_frame`
  (`PICK_FRAME_CURRENT`, `current=0/30`). It does not exist on the control,
  which passes all of its 45. #5115 was validated on Metal only. Every other
  assertion passes, including the hover-parity, drag-pan and destroyed-entity
  picks and `settled_cursor_is_current` (30/30).
- **VoxelEditor gizmo reach (#5221).** Every assertion that needs a gizmo
  drag to move the part fails; the press registers and the part stays put.
  The control fails identically, so #5115 is not the cause. The mechanism is
  not measured.

## Image acceptance

`python3 scripts/render-verify.py --target <T> --no-build --timeout 600 --json-out …`
with no `--update-references`:

- ShapeDebug: `all 103 checks PASS (83 skipped — references pending)`.
- CanvasStress: `all 31 checks PASS (28 skipped — references pending)`.

No row failed, so there is no inherited reference difference to classify. The
skipped rows have no `windows-debug` reference committed. They are absent
from this host's gate, not differences from it. A first ShapeDebug attempt
exited 1 with `render-compare errored: … screenshot_000014.png`. The driver's
own concurrent `--gui-test` repeat had moved the shared screenshot directory
aside mid-capture. The recorded run was repeated with nothing else touching
ShapeDebug.

`python3 scripts/light-verify.py --if-changed`: `0 of 34 changed path(s) in
the trigger surface; not required`.

## Kept frames

`frames/` holds the eight `--rotation-pick-test gui` captures (1280×720),
named by shot. The rotated fixture voxel sits under the injected cursor at
20°, 70°, 160° and 250°, and each pairs with its cardinal. Their SHA-256
digests are in `results.json`. No other frames are kept.
