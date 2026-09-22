# Continuous yaw as a profiled fixture

The objective's headline criterion
([`million-entity-render`](../design/objectives/million-entity-render.md)) is a
frame time **under a continuous yaw sweep**, and every committed million
number so far is a static pose. `IRPerfGrid --yaw-step <radians>` makes the
sweep a fixture: frame N renders at `--yaw + (N − 1) × step`. The step is per
rendered frame and not per second, so every run of a sweep renders the same
poses whatever its frame time, and the yaw is set absolutely, so frame N's pose
carries no accumulated rounding.

```bash
IRREDEN_BUILD_DIR="$PWD/build-release" python3 scripts/perf/repeat_profile.py \
    --output save_files/perf/sweep -- --wave-freeze --no-overlay \
    --auto-profile 300 --config-preset configs/perf/million-profiling-off.lua \
    --yaw 0 --yaw-step 0.020943951               # a full turn, 1.2° a frame
```

`repeat_profile.py` checks a sweep from the report's run witness
([million-controls.md](million-controls.md) § The run witness and its
controls): the frame count against `--auto-profile`, the first frame at
`--yaw` (or at `--yaw-first-frame`), the last at `--yaw + (frames − 1) ×
step`, the travelled arc equal to `(frames − 1) × |step|` within 0.05°, the
overflow lane sampled, and nothing dropped. A 300-frame full turn reads
`first=0.000 last=-1.200 travel=358.800`, which is 299 × 1.2° exactly.
`million_controls.py` runs this sweep as a third pose beside 0° and 45°.

## A driven yaw pins its pivot

The camera yaws about a focus. With no explicit focus the engine derives one,
the surface point under the viewport centre, on a yaw-settled frame or on the
first frame of a rotation that starts from a settled yaw of zero, holds it
otherwise, and falls back to the iso-depth-0 point under the centre before the
first derive (`DefaultPivotLatch`, `IRRender::getDefaultRotationPivotFocus`).
A continuous rotation therefore derives once, at its start, which
`DefaultPivotLatch.ContinuousRotationDerivesOnceAtItsStartAndNeverPerFrame`
asserts. That is right for a person turning the camera. For a fixture it means
the focus, and so the part of the world a given yaw shows, depends on how the
run began.

So `--yaw-step` pins the pivot at the origin, which is the grid's centre
(`IRRender::setRotationPivotFocus`; an explicit focus bypasses the latch), and
`--pivot-origin` does the same for a static `--yaw`; `million_controls.py`
passes it on every arm so the static and swept poses frame the scene the same
way. The view is then a function of the yaw alone, and the scene stays centred
through the turn.

The control for the pin is one pose reached from two first frames.
`--yaw-first-frame <radians>` renders frame 1 at a pose of its own and then
follows `--yaw`, `--capture-frame N` requests a screenshot after frame N, and
`--default-pivot` keeps the engine's pivot under a driven yaw. Held at 46.8°
and captured after frame 60
([continuous-yaw-sweep/pivot-identity/](continuous-yaw-sweep/pivot-identity/),
stage profiling on):

| Pivot | Frame 1 | Visible candidates at the held view | Lit pixels | Steady p50 ms | The two captures |
|---|---|---:|---:|---:|---|
| Default | 0° | 482,966 | 50.0% | 28.28 | 80.04% of pixels match, maximum delta 134: |
| Default | 46.8° | 626,223 | 62.9% | 31.95 | the scene is translated about 330 pixels |
| Pinned | 0° | 909,433 | 89.8% | 39.18 | 100% match, maximum delta 0: |
| Pinned | 46.8° | 909,433 | 89.8% | 39.32 | byte-identical |

Coverage and candidates move together (0.56 : 0.70 : 1 of the screen lit,
0.53 : 0.69 : 1 of the candidates), so the cheaper arms are cheaper because
less of the grid is on screen. The report's `Camera pivot:` line says which
pivot a run rendered with, and `repeat_profile.py` refuses a run whose pivot
is not the one its flags ask for. The default-pivot arm that starts at 46.8°
and never moves still changes view during the run: its first cull samples
read 909,433, the pinned view, and its held view reads 626,223.

The first sweeps ran unpinned, and they are kept under
[continuous-yaw-sweep/unpinned/](continuous-yaw-sweep/unpinned/) because they
read as engine findings and do not survive the pin:

![Frame time and fixed updates per frame for the same full-turn sweep with the pivot pinned and with the default pivot](continuous-yaw-sweep/sweep-frames.svg)

| Full turn from 0°, 1.2° a frame | Steady mean ms | Worst steady frame ms | Frames at the 8-update clamp | Median ms, frames 3 to 75 |
|---|---:|---:|---:|---:|
| Default pivot | 95.80 | 602.26 at 91.2° | 65 | 29.1 |
| Default pivot, replicate | 98.10 | 739.10 at 91.2° | (no tick series) | 29.6 |
| Pivot pinned | 41.11 | 92.43 at 271.2° | 0 | 39.9 |
| Pivot pinned, replicate | 40.81 | 88.30 at 91.2° | 0 | 39.1 |

What the unpinned sweep does, described and not explained: its first quadrant
gets steadily cheaper, from 43 ms to 21 ms, and the frame on 90° is the
cheapest of the run (18.7 ms); the next frame, at 91.2°, takes 602 ms (739 in
the replicate); the quadrant after it runs at about 165 ms a frame with the
GPU frame envelope averaging 59.6 ms over the run where the pinned sweep has
28; and the cost steps down again at 181.2° and at 271.2°. The update systems
cost 2.5 ms a tick in that run, so the eight-update clamp those frames sit at
is an effect of 165 ms frames and not their cause. The latch policy above says
the pivot is not re-derived on the 90° frame, so a re-latch there is not the
explanation, and nothing committed here establishes what is. The per-frame
update-tick series in the report is what made the clamp visible.

## What the pinned sweeps show

Apple M4 Max, Metal, macOS 26.5.2, AC power, Release, stage profiling off, the
million scene (100³ entities, 128³ pool, zoom 4, frozen wave). The manifests
record head `1cc64d0ce` with `creations/demos/perf_grid/main.cpp` modified:
the pin was measured before it was committed, and the commit after it is that
source. Shaders `6a13afcaf26f7615`, host load 2.7 to 4.5 on 14 CPUs with the
fleet live, so these are not reference milliseconds. Reports and manifests:
[continuous-yaw-sweep/](continuous-yaw-sweep/). The two one-update manifests
name their preset as `<docs/perf/continuous-yaw-sweep/million-one-tick.lua>`:
the run used a scratch copy of that file, and its local path was replaced in
the manifest after the run.

| Sweep (300 frames, 1.2° a frame) | Steady mean ms | Steady p99 ms | Worst steady frame ms | Updates / frame | GPU frame ms | Overflow peak / dropped |
|---|---:|---:|---:|---:|---:|---:|
| Through the cardinals (`--yaw 0`) | 41.11 | 87.63 | 92.43 | 2.5 | 28.03 | 971,724 / 0 |
| Through the cardinals, replicate | 40.81 | 78.41 | 88.30 | 2.4 | 27.78 | 971,724 / 0 |
| Half a step off them (`--yaw 0.010471976`) | 41.08 | 56.96 | 57.27 | 2.5 | 28.35 | 2,208,000 / 0 |
| Through the cardinals, `max_update_ticks_per_frame = 1` | 37.88 | 75.71 | 76.69 | 1.0 | 27.89 | 971,724 / 0 |

1. **Zero overflow drops across a full turn, in the objective's build.** All
   300 poses, in a build that logs nothing, dropped nothing. The lane's peak
   is 2,208,000 entries of 8,388,608 (26%) in the sweep whose frames land on
   the exact diagonals and 971,724 (12%) in the sweep that steps past them
   (44.4°, 45.6°). The witness keeps a run maximum and not a per-frame series,
   so "the peak is at 45°" is an inference from those two maxima; finding 3's
   frame times point the same way. The objective's row asks for zero drops
   across a 15-pose sweep; this is that reading on Metal at twenty times the
   poses.
2. **Away from two special poses, rotation costs the same at every yaw.**
   Quadrant medians are 39.1 to 41.3 ms in the three sweeps at 2.4 to 2.5
   fixed updates a frame, and 36.6 to 37.6 ms in the one-update sweep.
3. **The two special poses are the frame after a cardinal and the frame on an
   exact diagonal, and each is its sweep's tail.** The first rotated frame
   after 90°, 180° and 270° reads 78 to 92 ms, and it is the p99 (78 to 88 ms)
   of the sweep through the cardinals; without those three frames that
   sweep's p99 is about 46 ms. The frame on a cardinal itself is ordinary (34
   to 39 ms). The sweep half a step off the cardinals lands instead on 45°,
   135°, 225° and 315°, which cost 53.8 to 57.3 ms against 38.7 to 43.1 for
   their neighbours, and those frames are its p99 (57 ms); the sweep through
   the cardinals steps over them (41.0 ms at 44.4°, 42.5 at 45.6°). The
   diagonal is also where the overflow lane more than doubles. The per-axis
   set was released on a cardinal frame and allocated again on the next;
   § The crossing frame, split shows the long frame goes away when it stays
   resident and live, and § The crossing frame, with the set parked shows
   that the cost is the re-allocation and not the path re-entry.
4. **The fixed updates are worth about 3 ms of a 41 ms frame.** With the clamp
   at one update a frame the sweep reads 37.88 ms against 41.11 at 2.5, GPU
   frame envelope unchanged. That is D5's first number at this fixture: the
   objective's "≤ 1 update per rendered frame" row is worth about 3 ms here
   and the other 38 ms is rendering, 28 of them on the GPU.

## The crossing frame, split

Same host, build, scene and pinned sweep as above, host load 2.5 to 3.0. The
per-axis canvases' `allocate` and `release` calls are timed into the report's
CPU phase table (`PerAxisCanvas::Allocate`, `PerAxisCanvas::Release`), on each
allocation-state transition. The experiment is a local patch, never
committed, that skips the release, so the sets allocated on the first rotated
frame stay resident for the whole turn:

```diff
     } else {
+        return;
         const IRRender::TimePoint start = IRRender::SteadyClock::now();
         axes.release();
```

| Through the cardinals, pinned | Frame on the cardinal ms | First rotated frame after ms | Second ms | Those three frames, mean ms | Steady p99 ms | 300 frames, s | Allocations / releases |
|---|---:|---:|---:|---:|---:|---:|---:|
| Lifecycle as it is | 34.6 / 38.9 / 39.0 | **88.0 / 77.3 / 84.7** | 46.0 / 47.7 / 47.6 | 167.9 | 77.28 | 12.11 | 4 / 3 |
| Lifecycle as it is, replicate | 37.4 / 40.1 / 38.8 | **77.7 / 92.9 / 78.3** | 46.4 / 51.2 / 47.7 | 170.2 | 77.74 | 12.35 | 4 / 3 |
| Release disabled | 55.9 / 57.3 / 59.2 | **47.5 / 48.6 / 45.6** | 44.3 / 43.6 / 42.9 | 148.3 | 55.86 | 12.17 | 1 / 0 |
| Release disabled, replicate | 58.7 / 59.9 / 57.0 | **46.0 / 48.6 / 48.5** | 42.1 / 41.8 / 50.5 | 151.0 | 58.66 | 12.42 | 1 / 0 |

(Three values a cell: the 90°, 180° and 270° crossings. The arms were
interleaved within 64 seconds of each other.)

- **With the sets left resident and live, the long frame goes away.** The
  first rotated frame after a cardinal costs 46 to 49 ms, not 77 to 93, six
  crossings of six in each arm, and the sweep's p99 falls from 77 to 56 to
  59 ms. Frame 2, the first allocation in every arm, reads 95.8 / 102.2 /
  102.0 / 95.7 ms, so the patched binary is otherwise the same.
- **The experiment removes two things at once.** With the release skipped,
  `isAllocated()` never turns false, so the cardinal frame keeps running the
  per-axis path and the frame after it re-enters nothing. It cannot tell
  release-and-re-allocation from re-entering the per-axis path (any state
  that path rebuilds on its first live frame). A sets-resident arm that still
  takes the cardinal fast path, which is the mechanism, is what separates
  them.
- **The net saving is about 19 ms a crossing, and nothing over the turn.**
  About 20 ms reappears on the experiment's own cardinal frame (56 to 60 ms
  against 35 to 40), where the per-axis path runs at zero residual: the
  overflow lane's peak is 2,606,838 entries in both release-disabled runs
  against 971,724 as it is, more than the 2,208,000 of an exact diagonal, and
  only the cardinal frames can have produced it. Over the three frames of a
  crossing the arms differ by 167.9 and 170.2 ms against 148.3 and 151.0, and
  over all 300 frames by nothing outside the replicate spread. What moves is
  the tail. No arm here has both a fast-path cardinal frame and a cheap frame
  after it, so "about 37 ms on a cardinal and about 47 after it" is a
  projection for the mechanism and not a measurement.
- **Where the time goes is not located.** `allocate` costs 1.3 ms and
  `release` 0.7 to 1.1 ms of CPU, and the worst GPU frame envelope is 42 to
  48 ms in all four arms, so the extra 35 ms or more of a 77 to 93 ms frame
  is inside neither the calls nor the command-buffer span. A driver that
  defers an allocation's cost to first use would look like this; the report
  has no per-frame CPU and GPU split to say so.

## The crossing frame, with the set parked

The separating arm. `C_PerAxisTrixelCanvases` parks its set on a cardinal
frame (`park`: the live fields swap into `parked_`, so `isAllocated()` reads
false and all seven readers take the single-canvas fast path), swaps it back on
the next rotated frame (`unpark`) and frees it only after
`IRPrefab::PerAxisCanvas::kParkedCardinalFrames` (120) consecutive cardinal
frames. The report's CPU phase table gains `PerAxisCanvas::Park` and
`::Unpark` rows, so a run vouches for which lifecycle it ran. Same scene, build
type and pinned sweep as above; Release, stage profiling off; battery power at
85% falling to 82%, host load 2.99 to 3.34 with the fleet live; the four arms
ran interleaved between 19:28:44 and 19:31:44, control first. The control
binary is master `79ca9e3c6`'s Release build (`2d01e5b1…`), copied out of
the tree before the rebuild, so its manifests read this branch's dirty files
under `changes` and a scratch path under `build_dir`; the parked binary is
`18ab1d21…`. Reports and manifests: `crossing-control`,
`crossing-control-replicate`, `crossing-parked`, `crossing-parked-replicate`
under [continuous-yaw-sweep/](continuous-yaw-sweep/).

| Through the cardinals, pinned | Frame on the cardinal ms | First rotated frame after ms | Second ms | Those three frames, mean ms | Steady p99 ms | Steady p99 without the three crossing frames ms | Steady max ms | Steady mean ms | Allocate / Release / Park / Unpark |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Lifecycle as it is (control) | 37.5 / 39.7 / 40.2 | **97.5 / 96.9 / 93.0** | 48.1 / 49.7 / 50.3 | 184.3 | 93.01 | 48.14 | 97.5 | 41.56 | 4 / 3 / — / — |
| Set parked | 35.2 / 39.5 / 36.7 | **43.5 / 45.8 / 48.1** | 44.4 / 46.3 / 42.4 | 127.3 | 48.09 | 47.73 | 48.9 | 42.15 | 1 / 0 / 3 / 3 |
| Lifecycle as it is, replicate | 38.5 / 41.2 / 40.4 | **83.9 / 85.4 / 80.5** | 51.0 / 51.5 / 50.7 | 174.4 | 80.52 | 50.74 | 85.4 | 42.90 | 4 / 3 / — / — |
| Set parked, replicate | 37.5 / 37.3 / 37.4 | **42.7 / 44.2 / 43.6** | 44.2 / 41.6 / 43.4 | 124.0 | 49.44 | 49.44 | 52.6 | 41.35 | 1 / 0 / 3 / 3 |

(Three values a cell: the 90°, 180° and 270° crossings, frames 76, 151 and
226 of the sweep. Rows are in the order the arms ran. The overflow lane peaks
at 971,724 entries with nothing dropped in all four.)

- **The cost was freeing and re-creating the set.** With the set parked, the first rotated
  frame after a cardinal reads 42.7 to 48.1 ms against 80.5 to 97.5 as it is,
  six crossings of six in each arm, and it is an ordinary rotating frame: the
  steady mean is 41 to 42 ms and the second frame after the cardinal 41.6 to
  46.3. The cardinal frame itself stays on the fast path (35.2 to 39.5 ms;
  Park 3, Unpark 3, Release 0, Allocate 1 in the report). This is the arm the
  split experiment could not produce, a fast-path cardinal frame followed by a
  cheap one, and it says the per-axis path rebuilds nothing on its first live
  frame that costs more than any other frame; what cost 40 to 55 ms was
  freeing and re-creating the textures and buffers, of which the timed
  `allocate` and `release` calls themselves are 1.3 and 1.1 to 1.2 ms. The
  experiment removes the free and the create together and does not say which
  of them a driver charges to the next frame.
- **The crossing is no longer the sweep's tail.** Steady p99 falls from 93.0
  and 80.5 to 48.1 and 49.4 ms, and removing the three crossing frames moves
  it by 0.4 and 0.0 ms where it moved it by 44.9 and 29.8 as it is. What is
  left is the ordinary rotating tail. The exact diagonals, which this sweep
  steps over, are the next tail to measure.
- **A crossing saves 50 to 57 ms over its three frames in running order and
  the turn saves nothing outside the spread.** 184.3 and 174.4 ms against 127.3 and 124.0
  over the three frames; the four steady means in running order are 41.56,
  42.15, 42.90 and 41.35, three crossings of about 50 ms in a 12.5 s turn
  being about 1.2%. The release-disabled experiment above saved about 19 ms a
  crossing; the difference is the cardinal frame, which that arm ran at 56 to
  60 ms on the per-axis path and this one runs at 35 to 40 on the fast path.
- **The control's second frame after a cardinal is also raised**, 48.1 to
  51.5 ms against about 42, and the parked arms' is not, so part of the
  re-allocation's cost lands a frame late, which is the deferred first-use cost
  the split section named as one possibility.
- **What these runs do not say.** They ran on battery with the fleet live, so
  no millisecond is a reference; the first parked arm's frame 2, the one
  allocation, read 168.9 ms against 101 to 127 in the other three arms, and
  its GPU envelope maximum reads 86.0 ms against 39.0 to 56.6, a run maximum
  the report cannot place on a frame; no crossing frame in that arm exceeds
  48.1 ms. One replicate per arm. The parked set costs its GPU memory for up
  to 120 cardinal frames after a turn ends (about two seconds at 60 fps); a
  scene that never rotates allocates nothing. The window's control on the real
  binary is [parked-release-window/](continuous-yaw-sweep/parked-release-window/):
  one rotated frame (`--yaw-first-frame 0.5`) and then 199 cardinal frames
  read Allocate 1, Park 1, Release 1, and the same flags held for 70 frames
  (`parked-identity/parked-cardinal-parked.txt`) read Allocate 1, Park 1 and no
  release.

**Pixel identity.** Six single-capture runs on the same scene and build, pivot
pinned, `--capture-frame` through `fleet-run` directly because
`repeat_profile.py` refuses it; reports under
[continuous-yaw-sweep/parked-identity/](continuous-yaw-sweep/parked-identity/),
the three distinct captures and the diff full-size under
`docs/pr-screenshots/claude/million-entity-render-parked-per-axis-set/`. Every
arm adds `--wave-freeze --no-overlay --config-preset
configs/perf/million-profiling-off.lua`.

| Arm | Flags | Lifecycle rows in the report | Capture SHA-256 |
|---|---|---|---|
| `parked-cardinal-parked` | `--yaw-first-frame 0.5 --yaw 0 --auto-profile 70 --capture-frame 60` | Allocate 1, Park 1 | `d145106255ee8f8d` |
| `parked-cardinal-fresh` | `--yaw 0 --pivot-origin --auto-profile 70 --capture-frame 60` | none | `d145106255ee8f8d` |
| `base-cardinal-parked` (control binary) | `--yaw-first-frame 0.5 --yaw 0 --auto-profile 70 --capture-frame 60` | Allocate 1, Release 1 | `d145106255ee8f8d` |
| `parked-unpark-sweep` | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 77` | Allocate 1, Park 1, Unpark 1 | `34449f70093ec346` |
| `parked-static-91p2` | `--yaw 1.591740276 --pivot-origin --auto-profile 80 --capture-frame 77` | Allocate 1 | `34449f70093ec346` |
| `base-unpark-sweep` (control binary) | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 77` | Allocate 2, Release 1 | `8e1b1ab979d07c2f` |

- A cardinal frame rendered with the parked set resident is byte-identical to
  one with nothing ever allocated and to master's after its release: the
  parked set is never bound, and the fast path does not see it.
- The frame after the crossing, the first on the unparked set (91.2°, frame 77
  of the sweep), is byte-identical to the same pose held from frame 1.
- Master's frame after the crossing, the first on a fresh allocation, is not
  the pose it shows: 32,300 of 3,686,400 pixels differ from the settled frame
  by 1 to 7 across the whole grid and 304 by 32 to 54
  (`rotated-91p2deg-control-vs-parked-diff.png`; `render-compare.py` at its
  default tolerance of 8 reads 99.9938% match). With the set parked the frame
  is exact. The one input the overflow sort reads that differs between the two
  frames is the completed-frame entry count that bounds its encoded merge
  stages, 0 on a fresh allocation and the pre-park frame's count after an
  unpark; whether that is the mechanism is not established here, and a
  rotation that starts from a settled cardinal still allocates fresh, so the
  first rotated frame of every turn is still that frame (issue 3660 in the
  tracker).
- `render-verify --target IRCanvasStress` on the Debug tree passes all 11
  checks at 100% match and maximum delta 0 against master's references; its
  default suite steps 45° → 0° → 0° → 30° with 60-frame settles, so its first
  cardinal shot renders with the set parked. The nine-yaw set
  (`IRCanvasStress --auto-screenshot 120 --pivot-origin --no-spin
  --no-auto-rotate --sweep-yaw 0 6.2831853 9`, 60 settle frames a pose, so
  every cardinal pose parks the set and every rotated one unparks it) is
  byte-identical at all nine poses between origin/master and this tree; the
  captures and both hash columns are under
  `docs/pr-screenshots/claude/million-entity-render-parked-per-axis-set/`.

## What this does not say

- No millisecond here is a reference. The quiet-host matrix is still owed
  (issue 3638 in the tracker covers what the benchmark lock excludes) and now
  carries the sweep arm.
- One Release run per row with one replicate of the main sweep; round-to-round
  spread of the sweep is not yet measured.
- What the unpinned sweep's long frame at 91.2° is. It is not a pivot
  re-derive, by the latch's own policy and test. This document establishes
  only that a fixture which drives the yaw must pin the pivot.
- The release-disabled replicate has one 113.0 ms frame at 159.6°, not a
  cardinal and larger than any crossing frame in the table, followed by a
  55 ms one. It is unexplained, and the steady p99 over 225 frames (the third
  largest value) hides it.
- The sweep arm of `million_controls.py` has been dry-run and its arguments
  run by hand; it has not run end to end across a Debug and a Release tree.

## Next measurements

1. The exact diagonals, the sweep's other tail: measured as a band around
   45° in [diagonal-pose-cost.md](diagonal-pose-cost.md), which names the
   stages that read the overflow lane and the separating experiment still
   owed. With the crossing frame gone it is the only pose that stands out of
   a full turn.
3. The sweep in the three-round quiet-host matrix, and a longer window than
   one turn for the tail.
4. The static 0° and 45° arms again with `--pivot-origin`, so the matrix's
   three poses share one framing.
