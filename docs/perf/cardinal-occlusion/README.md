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

## Remaining native validation

The display slept before image verification. Detached job
`20261116T153050Z-render-verify-3f76d6` and the post-wake-attempt job
`20261116T153144Z-render-verify-f66495` both exited 69 before capture:
`CoreGraphics online=1 awake=0 NSScreen=1; the engine will discover 0 monitors`.
No reference image was updated. Wake and unlock the Aqua display before
resuming. Headless Metal kernel tests do run on this host.

Pending: NONE/FULL/POSITION_ONLY reference identity, every first-frame motion
on/off pair, all-mode positive fire and its negative control, all five default
render target sets, cull/feeder verifiers, final million admission, and the
paired timing/no-regression controls. The legacy post-raster Hi-Z builder
still repeats pyramid work and needs measurement during optimization. OpenGL
native coverage remains pending on its own host (#3544).
