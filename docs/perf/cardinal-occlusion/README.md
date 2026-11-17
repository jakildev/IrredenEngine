# Cardinal occlusion probes (issue #4955)

These are preliminary Metal reports from an Apple M4 Max on AC power,
2026-11-16. Both accepted runs exited cleanly with `QUIET=guarded` under
`fleet-run`. They do **not** satisfy the final paired-performance or image
identity gates. A prior contaminated run was excluded.

## Phase 0

Source: `49605bbe460e1075b7236b269aab7c7c4a215efa` (winner census on
`345dbbf251fe6b78a2e5af80c0c24479493d664f`). Release IRPerfGrid:

```sh
fleet-run --release --timeout 60 IRPerfGrid --config-preset configs/perf/million.lua --auto-profile 60 --yaw 0 --wave-freeze --no-overlay --count-voxel-winners
```

`phase0-winner-census.txt` records 59 completed samples: 786,156 admitted
candidates and 13,369 distinct winners each time (1.7006%). The feasibility
count gate passes. The diagnostic forces election and adds a census kernel;
its timing rows are **not** a performance baseline. The count result was
[posted before implementation](https://github.com/jakildev/IrredenEngine/issues/4955#issuecomment-6920265321).

## Prototype probe

```sh
fleet-run --release --timeout 60 IRPerfGrid --config-preset configs/perf/million.lua --auto-profile 60 --yaw 0 --wave-freeze --no-overlay --occlusion-cull
```

`prototype-cull-probe.txt` is an early dirty-tree probe of the current-frame
implementation. It recorded 91,566 admitted election/color candidates
(11.65% of the 786,156 baseline); stage 1 still processes the full candidate
list. It is retained for diagnosis, not as final-head acceptance evidence:
there is no exact source/binary fingerprint for this prototype and no paired
control. Re-run on the committed head, collect image identity and the five
interleaved 300-frame Release pairs before claiming the performance gate.

## Current-master replay (2026-11-17)

The prototype was replayed on `44955ae9fc42a5877f885d284ab5914b5a6c1138`
as `c74ed7b958e254df02088be7c1bfdb18423534cf`. The replay preserves the
rotating-path tested-placement latch and stand-down, static-resident fixture,
and fog settings that landed after the prototype's original base.

The Debug build of IRPerfGrid, IRShapeDebug and IrredenEngineTest succeeded
(`20261117T221339Z-build-c4443c`); a second build of IRCanvasStress, IRFogDemo
and IRLightingSdfBlocker succeeded (`20261117T222647Z-build-4b99fa`). Six
focused native tests passed with no skips and `RESULT=CLEAN`, including the
production Metal compact and winner-count kernels. Shader parity and header
checks passed. The fire harness's 17 tests and core render harness's 96 tests
passed. Six additional demo-argument mock tests fail on both this tree and
the base: their subprocess mock records the macOS display preflight as a
demo launch.

The candidate identity job `20261117T221900Z-render-verify-b3194e` completed
all 63 checks, **53 passing and 10 failing**. Its table is retained in
`metal-resume-identity-2026-11-17.txt`. No reference images were changed:

- All NONE and POSITION_ONLY cull-off/on rows matched the existing reference
  exactly, as did the NONE chunk-only control.
- FULL's `zoom4_pan` and `zoom4_rot_pan` differ from the NONE references in
  both arms, with matching aggregate metrics. This does not yet prove the
  arms are identical. Capture the base's FULL frames under new labels rather
  than overwrite the existing NONE references.
- Both static-cycle arms fail their three rotated rows. Their cardinal rows
  pass. The cull-off control needed to distinguish a regression from baseline
  drift could not run; the earlier host preflight used an existing binary
  and is not a rebuilt base control.

The next control launch reported `Discovered 0 display monitors` and
`com.apple.hiservices-xpcservice` connection errors:

```sh
fleet-run IRPerfGrid --auto-screenshot 48 --mode voxel_set --no-overlay --subdivision-mode none --wave-freeze --static-cycle --no-static --no-occlusion-cull > /tmp/pool-7-4955-static-off.log 2>&1
```

No captures or clean-exit result were produced by that control. The launch
was not retried. Process inspection (`ps -axo pid,ppid,command`) and a single
attempt to terminate the logged child (`kill -TERM 15703`) were denied with
`operation not permitted`. The handoff records the still-unconfirmed child
cleanup; this is an environment/permissions block, not passing evidence.

The replay also encountered an overlap with PR #5131's GLSL helper insertion
beside the replaced occlusion comment. Retaining the accurate common comment
prefix allows both shader bodies to three-way merge while retaining both
mechanisms. The full publication overlap gate still needs a final-head run.

## Remaining native validation

The display slept before image verification. Detached job
`20261116T153050Z-render-verify-3f76d6` and the post-wake-attempt job
`20261116T153144Z-render-verify-f66495` both exited 69 before capture:
`CoreGraphics online=1 awake=0 NSScreen=1; the engine will discover 0 monitors`.
No reference image was updated. Wake and unlock the Aqua display before
resuming. Headless Metal kernel tests do run on this host.

Pending: resolve the current-master identity failures above, every first-frame
motion on/off pair, all-mode positive fire and its negative control, all five
default render target sets, light/cull/feeder verifiers, final million admission,
and the paired timing/no-regression controls. The legacy post-raster Hi-Z
builder still repeats pyramid work and needs measurement during optimization;
its binding effects are also a candidate to investigate for the rotated rows,
not an established cause. OpenGL native coverage remains pending on its own
host (#3544, confirmed open on 2026-11-17).
