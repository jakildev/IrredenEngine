# Cardinal winner-election attribution

The high-density cardinal path includes an expensive winner-election dispatch
between stage 1 and stage 2. Neither existing stage row covered it. The new
`voxelCardinalElect` row measures that dispatch plus its trailing storage
barrier, excluding winner allocation/clear/binding and per-axis election.

Three native Metal Debug profiles, 180 frames each, frozen 64³ wave voxels,
zoom 4, FULL/base 4 (effective subdivisions 16), yaw 0, origin pivot, shadows
on and overlay off:

| GPU scope | Mean ms | Range of run means ms |
|---|---:|---:|
| Stage 1 | 28.516 | 28.471–28.570 |
| Winner election | **26.799** | 26.725–26.920 |
| Stage 2 | 25.545 | 25.437–25.751 |

Each stage has 179 resolved samples per run. Steady whole-frame mean is
89.027 ms over 405 frames, with pooled p99 92.353 ms; this is far above the
16.667 ms target. There is **no optimization claim**. These are sampled GPU
invocations, not whole-frame totals; other scopes can overlap, so do not sum
all report rows to infer GPU busy time.

```bash
python3 scripts/perf/repeat_profile.py --output /tmp/cardinal-election --repeats 3 --timeout 600 -- --mode voxel_set --wave-freeze --auto-profile 180 --grid-size 64 --zoom 4 --subdivision-mode full --base-subdivisions 4 --wave-amplitude 5 --pivot-origin --no-overlay --yaw 0
```

[Summary](summary.md) and `raw.tar.gz` retain every fresh report, run log,
manifest and the timing-source patch. Profiles were launched at commit
`04dbbd747ffe0df7bdb6749e5adc420fb2ab2319` plus the uncommitted timing change;
remaining dirty paths in the manifest were documentation/evidence. Binary
SHA-256: `c9b52e2dacbfaadc3c0457aa1f1b1f24202a9b6dce288d7318c2bf9b43694bb1`.
The patch was retained after changed-line formatting; no semantics changed.
Host: 14 CPUs, macOS 26.5.2, AC power, one-minute launch loads 3.17–3.86.
Fleet benchmark reservations were used; this is not quiet-host Release or
million-entity qualification. Cardinal geometry does not use the per-axis
overflow path, so its report's zero overflow sample count is not a queue test.

[Paired full-resolution captures](../../pr-screenshots/codex/cardinal-election-timing/README.md)
are RGB-identical with profiling on/off at both cardinal and diagonal poses.
Those separate runs contain screenshot readback and supply no performance
comparison. Mock device tests check that the new timer writes only its own
field/accumulator; parser coverage keeps election separate from other rows.

## Follow-up order

First resolve the restricted high-density cardinal viewport visible in the
paired captures. Pixel equality certifies the timer change, not that geometry
coverage is complete. The main-canvas scale/allocation contract needs an
independent extent check before comparing dispatch optimizations.

Then measure packing more micro-slices into each high-density cardinal workgroup
across **all three** raster/election dispatches. Every `(voxel, face lane, u, v)`
sample must remain exactly once, including non-power-of-two density tails.
Keep lower-density, feeder and per-axis contracts unchanged; CPU program
selection, GPU indirect dimensions and backend threadgroup registries must
agree. Validate shadows, four cardinals, transitions, ties and fog before any
speedup claim. Packing is proposed, not implemented or proven beneficial.
