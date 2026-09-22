# The first rotated frame of a turn is sorted short

Issue 3660 in the tracker records that master's first rotated frame on a
freshly allocated per-axis set is not the frame the same pose renders once
settled (32,300 pixels by 1 to 7 and 304 by 32 to 54 at 91.2° after a
re-allocation mid-sweep). Parking the set removes the case where a turn
crosses a cardinal; a turn that starts from a settled cardinal still
allocates fresh. This document reproduces that case as the first rotated
frame of a turn and tests the one input the overflow sort reads that a fresh
allocation zeroes: `C_PerAxisTrixelCanvases::laggedOverflowCount_`, the
completed-frame entry count that bounds how many merge stages
`detail::overflowSortDispatchSpan` encodes.

## Method

The million scene, Release, stage profiling off, pivot pinned, one capture a
run through `fleet-run` (`repeat_profile.py` refuses `--capture-frame`), all
with `--wave-freeze --no-overlay --config-preset
configs/perf/million-profiling-off.lua`. Two arms a pose: **fresh**, frame 1
on the cardinal (`--yaw-first-frame 0` at 46.8°; at 1.2° the sweep's own
`--yaw 0` puts frame 1 there) and frame 2 the first rotated frame on the set
allocated that frame, captured after frame 2; **settled**, the same pose held
from frame 1 and captured after frame 77. Two binaries: the unpatched
`18ab1d21…` (the parked-set build, the same source as the branch before its
format pass) and **fullspan** `3cc55978…`, the same source plus
[fullspan.patch](first-rotated-frame/fullspan.patch), which makes
`overflowSortDispatchSpan` return the cap so the sort encodes every merge
stage whatever the lagged count; the patch was applied and reverted around
the build and never committed. Host load 6 to 11 with the fleet live and the
battery at 28 to 30%: identity does not care about either. Reports, the
per-arm provenance and the patch under
[first-rotated-frame/](first-rotated-frame/), captures full-size under
`docs/pr-screenshots/claude/million-entity-render-first-rotated-frame/`.

| Arm | Flags | Binary | Lane max entries | Capture SHA-256 |
|---|---|---|---:|---|
| fresh 1.2° | `--yaw 0 --yaw-step 0.020943951 --auto-profile 80 --capture-frame 2` | unpatched | 971,724 (the run sweeps on to 94.8°) | `5c707d6a6babfe8d` |
| settled 1.2° | `--yaw 0.020943951 --pivot-origin --auto-profile 80 --capture-frame 77` | unpatched | 971,724 | `eafdfe3ad6993206` |
| fresh 46.8° | `--yaw-first-frame 0 --yaw 0.816814 --auto-profile 80 --capture-frame 2` | unpatched | 281,062 | `e84eccb8cfd26e39` |
| fresh 46.8°, run again | the same | unpatched | 281,062 | `10a159aaa104fc76` |
| settled 46.8° | `--yaw 0.816814 --pivot-origin --auto-profile 80 --capture-frame 77` | unpatched | 281,062 | `f42cdb630fb35288` |
| fresh 46.8° | as above | fullspan | 281,062 | `f42cdb630fb35288` |
| settled 46.8° | as above | fullspan | 281,062 | `f42cdb630fb35288` |

## What it shows

- **The first rotated frame of a turn differs from the settled pose, on the
  unpatched binary, and differs from itself run to run.** At 46.8°, 3,564 of
  3,686,400 pixels differ by 1 or 2; a second run of the same arm produces a
  third frame (`10a159aaa104fc76`), 3,484 pixels from the first and 4,172
  from the settled pose. At 1.2°, 3,128 pixels differ by 1. The magnitude
  does not track the lane: the 1.2° lane holds three times the 46.8° count
  and differs on fewer pixels, and master's re-allocation frame at 91.2°
  (32,300 pixels, 304 of them by 32 to 54) is larger than either.
- **With every merge stage encoded, it is the settled frame, every time.**
  The fullspan binary's first rotated frame at 46.8° is byte-identical to
  the settled pose, and the settled pose is byte-identical between the two
  binaries, so the patch changes nothing once the lagged count is real. The
  settled capture is also `f42cdb630fb35288`, the pinned capture recorded in
  [continuous-yaw-sweep/pivot-identity/README.md](continuous-yaw-sweep/pivot-identity/README.md),
  taken on a different build of the same shaders.
- So at 46.8° the difference is the sort's encoded span. On a fresh
  allocation the lagged count is 0, `overflowSortDispatchSpan(0, cap)` opens
  the minimum span of 2,048 entries and `forEachOverflowSortMergeStep` then
  opens no merge stage at all, so the lane is drawn in its append order,
  which the GPU's atomics decide and which changes run to run; that is what
  the two fresh captures show, and what
  `OverflowSortHandlesFirstPopulationAndCountTransitions` in
  `test/render/gpu_compute_dispatch_test.cpp` records for `count=4097,
  lagged=0`. The 1.2° pair has no fullspan arm and is read by the same
  mechanism, not shown by it; PR 3671's own control at 91.2° on master's
  lifecycle (a sweep frame 77 byte-identical to the settled pose once the
  full ladder is encoded) is the independent case at a third pose. The frame
  after an unpark carries the pre-park count and is exact
  ([continuous-yaw-sweep.md](continuous-yaw-sweep.md) § The crossing frame,
  with the set parked, Pixel identity).

## What this does not say

- How much the full span costs on the frame that needs it. Forcing it every
  frame is not the fix: a rotating frame whose lane is genuinely empty would
  encode every stage for nothing. The fix is a lifecycle decision, taken in
  the campaign doc: the first live frame after an allocation encodes the full
  span, or reads its own count back once, and every later frame keeps the
  lagged bound. PR 3671 in the tracker carries that change; the captures above
  are a second gate for it beside its own.
- The 91.2° re-allocation case on master is not re-run here; the parked set
  removes it, and PR 3671's control covers it.
- One repeat of one arm. Two fresh captures that differ from each other and
  a fullspan pair that does not is the evidence for nondeterminism; a third
  fresh run that matched one of the two would weaken it.
