# Compact per-axis resolve scratch

The candidate removes the unused aligned mask region, preserves winner IDs at
zero, and places the control block and overflow records directly after them.
Store, sort, lighting and fog obtain their uniform layout from one accessor.
There are no shader changes or changes to the overflow capacity.

## Visual controls

All 27 paired IRCanvasStress captures are RGB-identical: 12 default shots and
15 compare shots. Full PNGs, run logs and binary/shader hashes are retained.
`image-diff.json` reports changed-pixel counts. The baseline is
`edd6143ca3e8dbdc4ff0d3135ba2ead329740881`; `candidate-source.json` and
`candidate.patch` identify the C++ changes present in the candidate binaries.
The capture manifests track dirty shader sources only, so their empty
`source_dirty` arrays do not imply an unchanged C++ binary.

Capture commands are in each manifest. Both use IRCanvasStress with
`--auto-screenshot 120`; compare additionally selects `--only compare`.
Visual equality establishes no regression in these controls, not correctness
of every existing rendering artifact. Intermediate-angle face ownership and
other pending receiver paths remain on the rendering audit worklist.
No clip is included: these are deterministic fixed-shot allocation controls.

## Allocation and timing

For `N` axis cells, the saving per resident store is
`ceil(N / 64) * 64 * sizeof(uint32_t)` allocated bytes and frame-prefix clear
bytes. For example, 1024×2048 cells save 8 MiB. This is a layout example, not a
measured allocation for the profile below. The scratch buffer is shared across
the three axes; the saving must not be multiplied by three. A parked store
retains its allocation saving without a per-frame clear.

Three runs per revision used 262,144 voxel entities, fixed 45° yaw, zoom 1,
full subdivision mode, requested base subdivisions 4, and a frozen wave.
All runs rendered 180 frames; the first 45 were excluded from steady timing.
This is native Metal, macOS Debug on battery power, not a Release benchmark or
a moving-camera sweep. Full commands, host conditions, raw reports and logs
are retained under `perf/`.

| Measurement | Before | After |
|---|---:|---:|
| Mean of steady frame averages | 19.247 ms | 19.323 ms |
| Steady run min–max | 19.190–19.330 ms | 19.160–19.500 ms |
| GPU frame envelope | 15.382 ms | 15.171 ms |
| Maximum overflow records | 749,109 | 749,109 |
| Dropped overflow records | 0 | 0 |

These runs establish no reliable FPS improvement. Individual GPU stage
intervals overlap and must not be added or interpreted as isolated costs.

## Validation

- 21 targeted lifecycle, extent, and native GPU overflow-sort tests passed.
- Overflow append and visibility-routing script tests passed.
- Header checks: 601 headers, 40 Metal compute kernels, 97 GLSL files passed.
- All 18 native IRCanvasStress render checks passed, including the four
  independent cardinal geometry checks.
- Three focused code reviews found no actionable issue.
- Native Windows/OpenGL execution remains pending.
