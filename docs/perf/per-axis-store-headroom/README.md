# Per-axis store headroom cost

Release measurements for the content-sized per-axis store in PR #5101. The
base is the PR merge base (`e5c0ce713`); the head is `7600c4b24`. Both arms
were built independently with `fleet-build --release --target IRPerfGrid` on
macOS 26.5.2, Apple M4 Max, Metal, AC power.

Each row is five interleaved base/head pairs of 300 frames. Every retained run
reported `QUIET=guarded`, a clean exit, the requested yaw and explicit pivot,
zero overflow drops, and exactly 300 profile frames. Contaminated runs and the
known 299-frame close edge were discarded before grading.

The million control used:

```text
python3 scripts/perf/repeat_profile.py --target IRPerfGrid --release \
  --output <arm/pair> --repeats 1 --timeout 300 -- \
  --auto-profile 300 --wave-freeze --no-overlay --pivot-origin \
  --config-preset configs/perf/million.lua --yaw 0.785398163
```

The default 64-cubed control omitted `--config-preset` and used `--yaw 0.35`.

| Scene | Frame mean, base -> head | Ratio | Frame p99 mean, base -> head | Ratio | Verdict |
|---|---:|---:|---:|---:|---|
| million, yaw 45 degrees | 10.894 -> 11.014 ms | 1.011x | 11.762 -> 11.364 ms | 0.966x | pass |
| default 64-cubed, yaw 0.35 rad | 9.982 -> 9.684 ms | 0.970x | 12.576 -> 13.160 ms | 1.046x | pass |

The frame rows pass the locked 1.10x point-ratio gate. `comparison.json`
retains the five inputs per arm plus the paired delta mean, sample standard
deviation, and one-sided 95% lower confidence bound. The million control's
`voxelPerAxisStore` row rose from 0.7332 to 0.8112 ms (1.106x) as the larger
content-derived store became active; the whole-frame criterion remains well
inside its gate.
