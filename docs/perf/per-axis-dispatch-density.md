# Rotated face dispatch density

Continuous-yaw GRID rendering stores one sample per face-local cell. Presentation
subdivision changes the reconstructed screen footprint, not the number of source
samples in that store. The visibility compactor previously dispatched effective
subdivision squared micro-slices for each axis; every consumer discarded all but
slice zero after doing voxel reads and face selection.

The compactor now emits one micro-slice for per-axis lists, and the depth/colour
consumers reject the unused local-Z lanes before loading voxel data. At effective
subdivision 4 this reduces two Z workgroups to one (eight lanes per group), and
face-selection work from sixteen slices to one. Cardinal and shadow-feeder lists
retain their subdivision counts. The final scatter still reconstructs the same
finite face geometry; no filtering, silhouette expansion or shadow reduction is
involved. GLSL and Metal implement the same change.

## Native measurements

Apple M4 Max, macOS Metal Debug, IRPerfGrid 64³ (262,144 voxel entities), FULL mode,
base subdivision 1, default animated wave, lighting and finite-face shadows.
Each cell has three fresh 300-frame reports. Means describe observed runs, not
Release performance or a guarantee of identical host scheduling.

| Workload | Before frame ms | After frame ms |
|---|---:|---:|
| 45°, zoom 4 / effective subdivision 4 | 29.287 (28.490–30.290) | 21.883 (21.400–22.140) |
| 45°, zoom 1 / effective subdivision 1 | 22.427 (22.050–22.810) | 22.467 (21.940–22.760) |

The high-subdivision case uses about 25% less frame time. Its sampled GPU storage
mean falls from 6.732 to 5.077 ms, overflow from 4.345 to 2.590 ms, and finalization
from 5.598 to 2.898 ms. Scatter stays 2.995 versus 2.992 ms and voxel sun casting
0.679 versus 0.671 ms. Sampled stage rows are invocation timings, not additive
full-frame accounting. CPU fixed-step catch-up also changes with frame duration.

The cardinal zoom-4 control is inconclusive as an isolated timing comparison:
initial after runs average 14.297 ms, parent-shader runs 21.083 ms, another parent
batch 20.160 ms, and final after runs 14.357 ms. Unchanged light-volume work also
slows substantially in the parent batches (8.812 versus 5.680 ms in the first
comparison). These controls are retained rather than attributed to this change;
host/GPU state or scheduling effects have not been isolated. Cardinal images
remain identical, and the dispatch count in that path is unchanged. A stronger
performance harness should alternate variants and record clock/power state.

The executable hash is identical across before/after runs. The runner additionally
fingerprints the staged shader directory, because shader edits do not change the
executable. The original before hash was recorded after its three runs but before
shader editing/restaging; that timing is explicit in its manifest. Control runs
restore only the six changed staged shader assets from the parent commit, keeping
the working source and executable intact. Shader hashes distinguish those states
from the dirty-tree metadata. All assets are restored through fleet-build afterward.

Reports, manifests and summaries are retained in
[per-axis-dispatch-density/](per-axis-dispatch-density/). Full native logs remain
in the worktree save_files/perf directories. The command recorded in each manifest
is directly reproducible through scripts/perf/repeat_profile.py.

## Visual equivalence

Frozen-wave IRPerfGrid grid16, zoom4, FULL/base1, six warmup frames per pose:

```sh
fleet-run --timeout 120 IRPerfGrid --mode voxel_set --wave-freeze --grid-size 16 --zoom 4 --subdivision-mode full --base-subdivisions 1 --yaw-ramp --auto-screenshot 6
fleet-run --timeout 120 IRPerfGrid --mode voxel_set --wave-freeze --grid-size 16 --zoom 4 --subdivision-mode full --base-subdivisions 1 --yaw-ramp --yaw-ramp-wave --auto-screenshot 6
```

All 28 near-cardinal and 15 wide-angle scene images are RGB-identical. Comparison
uses x=0..1799 across the full 1440-pixel height of each 2560×1440 capture, excluding
only the live performance overlay on the right. Full representative screenshots
and every pair's scene hash are retained in
[the screenshot evidence](../pr-screenshots/codex/per-axis-dispatch-density/).
Near-cardinal pairs are captures 1–28 before / 29–56 after; wide-angle pairs are
72–86 before / 57–71 after. Full-frame differences are the dynamic overlay.

This proves output preservation in the exercised GRID camera paths, not all
rendering modes or OpenGL runtime behavior. Native Metal compiled the changed
kernels during the runs; OpenGL still needs cross-host execution.

## Remaining work

The subdivision penalty removed here was wasted dispatch work. Rotation still
processes repeated axis candidates and overflow, and this does not establish
rotation/cardinal parity. Continue the
[optimization TODO](rotation-subdivision-audit.md#proposed-optimization-todo):
consistent work counters, culling before expansion, matched-area and entity-rotation
matrices, light-volume cost, CPU sampling, and Release/OpenGL controls.
