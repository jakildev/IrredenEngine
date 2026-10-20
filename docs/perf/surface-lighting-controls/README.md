# Capture-free surface-lighting controls

Native Metal on Apple M4 Max, macOS 26.5.2, Debug, battery power,
2026-10-19. All 60 runs completed CLEAN and the outer resource guard returned
[GUARDED](quiet.txt). This is diagnostic cost attribution, not a Release,
Windows or million-entity throughput qualification. The crowded configurations
do not meet a 16.67 ms wall-frame budget on this setup.

## What ran

Twenty cases, three forward/reverse/forward rounds, 360 frames per run.
Each report contains exactly 360 frame times and camera samples; steady-frame
statistics exclude the first 90. The camera and subjects are frozen, AO is off,
and downward sun illuminates a source box above a GRID floor. Vary yaw 0/0.6,
zoom 1/4, subdivisions 1/4, and 0/8/16 coincident distant source casters.
Crowded shadows-off and unlit pairs preserve the scene and camera.
`IR_PERAXIS_SURFACE_LIGHTING=1` is explicit in every matrix environment.
Completed GPU samples certify the cardinal or per-axis lighting draw selected;
unlit controls must have neither surface-lighting row.

[cases.json](cases.json) records every argument and order. Each case/round
retains the full native report and manifest, including binary, shader and
runtime-script hashes, power source, timestamps and validation outcomes. The
binary was built from the dirty implementation of this change on base
`0955cfbf17e7d3dd6685b85b841203d896a08693`; all matrix runtime hashes match.
After this matrix, invalid-argument validation moved before engine construction
to avoid leaving a World alive on early rejection. The timed render path is
unchanged; the subsequent final-binary smoke is recorded separately rather
than relabeling these reports with a new executable hash.
All 20 [final-binary smoke cases](final-smoke/cases.json) completed CLEAN with
120 frames and valid route samples; their separate outer verdict is
[GUARDED](final-smoke/quiet.txt). These shorter runs are not pooled into the
three-round timing tables. Native negative controls for a zero frame count and
`--profile-frames 120 --auto-screenshot 5` both rejected the request with exit 2
before creating the World, without shutdown errors. The run wrapper labels
intentional nonzero CLI exits `CRASH`; these are rejection tests, not clean
render runs.
Inner manifests say `quiet: unknown` because the outer benchmark wrapper owns
the quiet window; its retained verdict is the qualification record.

```sh
fleet-build --target IRCanvasStress -- -j4
IR_QUIET_REPORT_FILE=/tmp/surface-lighting.quiet ir-acquire benchmark -- python3 scripts/perf/surface_lighting_controls.py --output /tmp/surface-lighting --rounds 3
```

Run native display applications with the required host permissions. An earlier
sandboxed launch found zero displays and timed out before rendering; it was
rejected. An earlier screenshot-driven matrix also failed its quiet guard and
included capture stalls. Neither is used in the tables here. The separately
retained [fixture images](../../pr-screenshots/codex/cardinal-lighting-controls/README.md)
document the subjects without putting captures into these measurements.

## Observations

- Cardinal zero-overlap steady frame means are about 8.33 ms at both zooms.
  Cardinal eight-overlap means are 20.65 ms at zoom 1 and 16.20 ms at zoom 4.
  The zero-overlap per-axis zoom-1 case varies substantially between runs;
  retain the range rather than selecting the best run.
- Sixteen-overlap lit, shadows-off and unlit steady means remain roughly
  20.9–21.5 ms across the two routes at zoom 4. Disabling lighting does not
  recover the frame budget in this fixture. This alone does not identify the
  remaining bottleneck.
- For cardinal zoom 1/eight overlap, the surface gather averages 0.736 ms per
  sampled invocation, private-canvas compositing 1.779 ms, and the frame GPU
  envelope 7.159 ms. Stage means are not additive frame totals, and the envelope
  includes stalls rather than measuring GPU busy time.
- Subdivision 4 raises the cardinal surface draw from 0.736 to 1.083 ms at
  zoom 1, and from 0.673 to 1.210 ms at zoom 4 for the eight-overlap cases.
  These are observations within this fixture, not a general scaling law.

Full results: [frame means](summary.md), [steady tails](budget-summary.md),
[sampled GPU stages](gpu-summary.md). The overflow column measures per-axis
raster overflow, **not** sun-face index completeness. Cull means are per-canvas
samples, not unique scene entity counts. The known incorrect sixteen-caster
fallback must not be credited as a speedup; see the
[coverage diagnostic](../../pr-screenshots/codex/crowded-shadow-overlap/README.md).

## Separate CPU sample

A [five-second native sample](cpu-sample/run-1.sample.txt) of cardinal zoom 1,
eight overlap ran during a separate 900-frame capture-free run. Its
[manifest](cpu-sample/manifest.json) records the exact command, target PID,
successful sample exit and target liveness. Its outer guard also returned
[GUARDED](cpu-sample/quiet.txt). Sampling perturbs execution; its frame timings
are not pooled with the matrix.

Of 3,511 main-thread samples, 1,873 (53.3%) were sleeping in offscreen pacing
and 1,025 (29.2%) were waiting for Metal command-buffer completion. Another 78
were in deferred buffer release. These are stack-sample proportions, not exact
phase durations or total CPU utilization. Worker-thread idle samples must not
be added to the main-thread denominator.

This supports separating presentation waits from simulation work before a CPU
rewrite. Offscreen pacing intentionally waits after overruns, and presentation
currently waits for GPU completion; removing either changes behavior and resource
lifetimes. Next qualify a visible Release run on AC power, measure private-canvas
compositing and retained-buffer costs, and evaluate bounded in-flight work only
with explicit lifetime and cross-backend validation. Preserve offscreen casters
when evaluating occupied-footprint display culling. The depth-preserving shadow
index and Windows reproduction remain separate correctness work.
