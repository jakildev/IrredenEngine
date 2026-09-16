# Native CPU call-stack profiling

`scripts/perf/repeat_profile.py` can retain macOS `sample` call stacks alongside
its CPU/GPU scope report. Sampling is opt-in and attaches only to the matching
demo beneath that run’s fleet wrapper, including wrappers that exec the demo.
The manifest retains the command, delay, result and target-survival check.
An unavailable sampler, failed process lookup or prematurely exiting target
fails the measurement while preserving the normal run log and fresh report.

```sh
python3 scripts/perf/repeat_profile.py --output save_files/perf/cpu-sample --repeats 1 --cpu-sample-seconds 4 --cpu-sample-delay 5 -- --mode voxel_set --grid-size 64 --wave-freeze --yaw 0.785398163 --zoom 4 --auto-profile 1800
```

The delay starts when the runner’s demo appears, not when scene construction
finishes. Choose enough delay and profile frames for the scene to settle and
remain alive throughout sampling. Native task-inspection permissions are needed;
a sandbox denial is a failed capture, not an empty successful trace. No sudo or
system configuration changes are performed. Linux/Windows CPU sampling is not
implemented by this switch; ordinary report capture remains portable.

## Initial observations

Apple M4 Max, macOS, Metal Debug, source `9bb55dc146adfb00ff50635d32fbbf8eb971a89e`,
64³ entities, yaw 45°, zoom/effective subdivisions 4. The frozen and animated
controls use the same executable and shader hashes. Each samples four seconds
at nominal 1 ms intervals after a five-second delay, within an 1800-frame run.
[Reports, manifests, sampler logs and excerpts](native-cpu-sampling/) retain the
evidence. Full native traces remain in the corresponding local run directories.

- Frozen: 2469 of 3333 main-thread observations were inside Metal’s
  `waitUntilCompleted` during presentation (74%).
- Animated: 2228 of 3285 main-thread observations were in the same wait (68%).
- Active stacks identify `UPDATE_VOXEL_SET_CHILDREN`, transform/modifier work,
  chunk world-bound rebuilding and `MetalBufferImpl::subData` copies. The frozen
  control still updates voxel positions; freezing the wave does not bypass that
  system. Animated-only work includes periodic-idle evaluation and modifier writes.
- Scope reports give `UpdateVoxelSetChildren` 1.808 ms/call frozen and
  1.947 ms/call animated, with 2225 and 2474 update calls respectively over 1800
  render frames. This is not a fixed update-count comparison.

These are sampled wall-stack observations, including blocked threads, not CPU
utilization percentages or exclusive CPU-time attribution. Worker-thread sample
counts overlap wall time and must not be summed with main-thread percentages.
Sampling and existing profiling add overhead; these runs establish attribution,
not a speedup. The first frozen trace has DWARF warnings retained in its sampler
log; useful engine symbols are resolved, but complete source-line attribution
is not claimed. GPU occupancy/bandwidth counters are still unproven.

The frozen run also reports overflow capacity saturation (371422 dropped entries
at cap 524288). Treat that workload as a saturated diagnostic, not complete
visual coverage. Reducing repeated records could remove this pressure, but the
pending duplicate-emission change still fails its small-scene visual control.

The final harness also passed a fresh frozen verification run with an explicit
post-sample target-survival check; its manifest, report and sampler log are in
`native-cpu-sampling/verified/`. No DWARF warning occurred in that capture.

## Next measured changes

1. Coalesce contiguous per-worker voxel update spans before main-thread cull
   invalidation and upload queuing. Preserve every written span, pool and upload
   policy, including GPU-transformed sets; avoid cached-transform dirty flags.
2. Measure chunk-bound rebuilding and buffer uploads separately after batching.
3. Add profiler-off and Release controls before making throughput claims; keep
   GPU wait time separate from active CPU work.

## Overflow seam investigation

Broader canonical sorting did not eliminate the 12-pixel seam. A temporary
lagged readback probe found identical unique lit `(cell, color, depth)` records
with duplicate and single emission, with exactly two versus one copy per record.
The probe itself changed the parent’s seam. This narrows the investigation but
does not prove equal-depth ordering as the cause. All experimental renderer
changes were removed; no sorting or tolerance workaround ships here.
