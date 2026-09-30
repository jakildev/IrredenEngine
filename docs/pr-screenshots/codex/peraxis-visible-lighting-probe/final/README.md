# Shared-program visibility prepass: corrected evidence

**All 26 native RGB comparisons pass.** The corrected candidate uses the same
fragment program for prepass and replay, selected by a runtime flag. It keeps
the original two-code depth allowance. The four previously failing sweep views
and all three failing zoom-4 profile pairs now match their references exactly.
The [initial evidence](../README.md) remains separate and retains the failures.

This is an opt-in optimization for attached per-axis surface lighting. The
prepass records minimum fragment depth and replay skips full lighting behind
that depth. Face geometry, shadow queries for retained fragments, and sharp
shadow edges are preserved in these captures. Both attached lighting and the
prepass remain default-off; these measurements do not establish broad adoption.

## Native parity

Metal Debug on Apple M4 Max, macOS 26.5.2, battery power, 2560×1440 output.
[comparison.json](comparison.json) records exact decoded RGB comparisons without
alignment, masking, resampling, or tolerance, plus PNG and RGB hashes.
All 32 full candidate PNGs are retained.

| Case | Comparisons | Changed RGB pixels |
|---|---:|---:|
| Identity frame, prepass enabled | 4 | 0 in every view |
| Mixed scene, prepass enabled | 5 | 0 in every view |
| Zero-sun HDR sky, prepass enabled | 5 | 0 in every view |
| Mixed scene, prepass unset | 5 | 0 in every view |
| Zoom-1 same-runtime off/on profile pairs | 3 | 0 in every pair |
| Zoom-4 same-runtime off/on profile pairs | 3 | 0 in every pair |
| Stats versus parent screenshot_003143 | 1 | 0 |

Sweep references are the corresponding
[parent captures](../../surface-shadow-query-gate/README.md). The stats reference
and its earlier runtime provenance remain in
[the initial package](../captures/stats/parent-reference-manifest.json).
This establishes parity for the listed views, not a new independent proof of
full-scene geometric correctness or native OpenGL behavior.

The unscaled detail below uses `frame-0.png` rectangle
`(1040,475)–(1510,955)`. It is exactly equal to the parent crop;
[detail.json](detail.json) records the operation.

![Corrected sharp-shadow frame detail](frame-detail.png)

![Full mixed scene at cardinal yaw](mixed-0.png)

The [depth diagnostic](../depth-diagnostic/delta.json) from the first attempt
identified a depth-code difference of 32 at all 96 missing frame pixels. The
same-program correction removes the observed image discrepancy without
widening the two-code allowance. These captures do not isolate a particular
compiler optimization as the cause.

## Counter proof

The corrected [stats log excerpt](captures/stats/run-1-excerpt.log) reports
`fragments=63930 retained=12664 rejected=51266 pixels=927048` at active frame 180,
identical counts to the first attempt's zoom-1 stats fixture. Rejected plus
retained fragments equal the prepass count: 80.2% skip full fragment lighting in
that frame. Its [capture](captures/stats/run-1.png) matches the parent exactly.
Counters and the synchronizing readback are used only in this diagnostic run;
its timing is not used below.

## Fresh performance runs

Three runs per configuration, 363 frames per run, fixed yaw 73.125°, fixed zoom,
subdivision 1, with stats unset. Both configurations at both zooms use the same
binary, staged shaders, and runtime scripts. Every run reports 363 valid GPU
samples, zero invalid samples, fixed camera witnesses, and zero per-axis
overflow drops. Peak overflow entries are 1,533 at zoom 1 and 111 at zoom 4.
[Profiles and summaries](perf/) retain all runs; [performance.json](performance.json)
collects the rounded measurements below.

| Measurement | Off mean (range), ms | On mean (range), ms |
|---|---:|---:|
| Zoom 1 GPU frame envelope | 9.852 (9.803–9.905) | 8.572 (8.564–8.580) |
| Zoom 1 per-axis scatter scope | 5.217 (5.205–5.232) | 3.960 (3.936–3.974) |
| Zoom 1 steady frame average | 13.617 (13.550–13.710) | 12.320 (12.230–12.390) |
| Zoom 4 GPU frame envelope | 4.922 (4.893–4.958) | 4.783 (4.733–4.823) |
| Zoom 4 per-axis scatter scope | 1.088 (1.072–1.103) | 0.948 (0.913–1.006) |
| Zoom 4 steady frame average | 8.867 (8.820–8.910) | 8.833 (8.830–8.840) |

The zoom-1 GPU frame envelope decreases 13.0% and the scatter scope decreases
24.1%. At zoom 4 the GPU envelope decreases 2.8% and scatter decreases 12.9%;
the steady frame ranges overlap, so there is no clear wall-clock improvement
at that zoom. The enabled scatter scope includes both prepass and replay.
These are sequential runs on a shared battery-powered host, not interleaved
statistical trials. Host load spans 5.41–6.10, and battery level spans 41–42%.
Other scopes vary; do not sum overlapping Metal scopes or interpret the frame
envelope as GPU busy time.

This bounded scene does not establish population or subdivision scaling,
performance at hundreds of thousands of entities, or a universal speedup.
The small GPU gain at zoom 4 reinforces retaining the opt-in gate.
The mixed fixture also retains an upstream `REBUILD_GRID_VOXELS` span-cap
warning: 12 of 1,740 covered destination cells are dropped. Zero per-axis
overflow drops describes a separate queue, not complete upstream occupancy.

## Reproduction and controls

[Runtime comparison](runtime-comparison.json) verifies coherent binary,
staged-shader, and runtime-script SHA-256 fingerprints across all nine final
configurations, including the frame sweep. Each manifest retains its exact
command, timestamps, host state, dirty source base and status, original-manifest
hash, screenshot mapping, and compact log excerpts. These runs use dirty base
`44352414ef8046651bfadf1c923d4224c380f365`; the staged runtime fingerprints identify
the candidate, not a claim that this base commit contains the edits. A run's
`clean` field describes runtime validation rather than Git status.

[launch-controls.txt](launch-controls.txt) preserves the sequential controller
for every configuration except the earlier shared-frame capture. All runs set
`IR_PERAXIS_SURFACE_LIGHTING=1`. Off-z1, off-z4, and mixed-off explicitly unset
`IR_PERAXIS_VISIBILITY_PREPASS`; the other final configurations set it to `1`.
Only stats sets `IR_PERAXIS_VISIBILITY_STATS=1`; all others leave it unset.
Sky uses `--probe-sky` without further environment changes. Environment
supplements are labeled because the original runner manifests omit inherited
environment; the frame launch was separately confirmed by the operator.

The updated GLSL and Metal CPU adapters execute 106,545 depth pairs per backend
and call one shared fragment body with the runtime pass bit. Eight shader
mutations fail, including confusing the pass and stats bits. Executed probe
selection controls pass. The C++ routing suite covers 32 configurations over
181 frames and rejects 20 lifecycle/order mutations, including a separate
prepass program. All 54 renderer suites passed before the correction; the three affected
suites were rerun and passed after it. Header checks passed for 599 C++ headers,
97 GLSL shaders and 40 Metal shaders; Ruff also passed. CPU adapters use serial
atomic models and cannot replace the native image comparisons for compiler,
derivative, raster, or concurrency behavior. Native OpenGL remains unverified.
