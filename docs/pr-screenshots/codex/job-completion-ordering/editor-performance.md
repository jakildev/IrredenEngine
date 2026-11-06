# Editor shadow cost after completion ordering fix

All 24 Release/Metal offscreen runs completed CLEAN on macOS 26.5.2 arm64, **AC Power**, with an outer benchmark exit of 0 and **GUARDED** quiet coverage. Each run recorded 360 valid frames and GPU-frame samples, constant requested yaw/zoom, and zero overflow drops. Binary, shaders, scripts, configuration and power stayed unchanged. The fixture has **219 authored voxels**; timing-on census reports **212 retained voxels** and 314 axis entries. Timing-off retained values of 0 in the generated summary mean **not measured**, not an empty scene.

Three forward/reverse/forward rounds compare sampled and continuous receiving at two poses, with GPU stage timestamps both on and off. Stage-timing-off retains GPU frame-envelope measurement. The editor UI remains present; AO, construction-grid/hover feedback, visibility prepass and query counters are off. Steady statistics exclude the first 90 of 360 frames per run; p95 pools the remaining 810 samples per case.

| Pose | Stage timestamps | Steady mean, sampled → continuous (ms) | GPU envelope mean, sampled → continuous (ms) | Steady p95, sampled → continuous (ms) |
|---|---|---:|---:|---:|
| 30°, zoom 4 | Off | 15.720 → 17.830 | 5.004 → 5.160 | 21.598 → 21.963 |
| 67°, zoom 8 | Off | 16.957 → 19.260 | 5.133 → 5.241 | 21.433 → 21.882 |
| 30°, zoom 4 | On | 10.940 → 12.803 | 4.343 → 4.481 | 18.836 → 21.243 |
| 67°, zoom 8 | On | 17.193 → 14.237 | 4.923 → 4.950 | 21.406 → 21.226 |

The three paired **continuous-minus-sampled steady wall differences**, in round order:

| Pose | Stage timestamps | Round 1 / 2 / 3 (ms) | Mean difference (ms) |
|---|---|---:|---:|
| 30°, zoom 4 | Off | +9.33 / −0.06 / −2.94 | +2.110 |
| 67°, zoom 8 | Off | +0.38 / +4.98 / +1.55 | +2.303 |
| 30°, zoom 4 | On | +6.55 / −0.91 / −0.05 | +1.863 |
| 67°, zoom 8 | On | +3.11 / −8.33 / −3.65 | −2.957 |

The stage-timing-off GPU-envelope increase averages **0.156 ms** at 30° and **0.108 ms** at 67°. Continuous surface-draw invocation means with stage timing enabled are **0.430 ms** and **0.579 ms**. Draw-stage rows are not additive frame costs. GPU envelope includes submission gaps and stalls; wall time also includes pacing and scheduling.

These three-run means support a bounded editor-quality decision, not a stable FPS or speedup claim. Wall differences vary substantially and change direction with instrumentation at 67°; many samples from both routes exceed 16.67 ms. Continuous receiving has a modest average GPU-envelope increase in this small fixture, but is not free and does not establish 60 Hz visible-editor performance, large-scene capacity, or another backend's cost. Keep the explicit sampled-lighting opt-out.

This fresh matrix is qualified; the earlier incomplete matrix and timeout diagnostics are excluded from all numbers above. `qualification.json`, `cases.json`, per-run manifests/reports and `paired-summary.json` retain the evidence. Release binary SHA: `39f79be0dd503f007490556d2c727342da11624fd544c0bcd7eac60590836b13`.
