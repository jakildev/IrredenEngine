# Light-volume timing attribution

The bounded-propagation control reports a dense light-system counter span of
70–75 ms while the three voxel raster/election rows already account for roughly
73 ms inside a 73–79 ms whole-GPU envelope. These rows cannot all be interpreted
as exclusive stage costs. The original measurements remain in
[bounded light propagation](bounded-light-volume/README.md).

## Reproduce

Use a built `IRPerfGrid` and a fresh output path:

```sh
ir-acquire benchmark -- python3 scripts/perf/timing_controls.py --output /tmp/light-timing-controls --rounds 2 --frames 120
```

The six cases hold a frozen 64³-entity scene fixed and compare counter timing
with synchronized whole-system timing at low density, dense cardinal and dense
45° yaw. The second round reverses case order. Complete Lua presets, commands,
raw reports, logs and runtime hashes stay with the output. `--pose low|dense|rotated`
restricts the matrix; `--rounds` and `--frames` control repeat/sample counts.

Both modes must provide sampled `computeLightVolume` timing. Counter mode must
also provide sampled `voxelCompact` timing, and synchronized mode must not.
Missing reports or mismatched modes fail the run. The shared runner also checks
clean completion, report freshness, pose and overflow evidence, while the matrix
requires identical binary/shader/runtime-script hashes, power source and render
environment.

The [timing contract](../design/gpu-stage-timing-cost-model.md#synchronized-attribution-control)
defines what each mode measures. A synchronized interval contains CPU encoding
and GPU waits and changes scheduling; it is not a GPU-only duration or a speedup.
The probe leaves renderer source, shader code and the binary unchanged.

## Execution status

The two-round native attempt on 2026-10-04 stopped before launching any case:
`ir-acquire` returned exit 75, `QUIET-REFUSED`, because another fleet worker
remained busy through the quiet-window drain timeout. There are no new native
timing measurements from this probe. CPU tests cover the case pairs, missing and
unsampled evidence, wrong-mode rejection across rounds, and summary treatment of
zero-sample rows. The native comparison remains pending on a quiet host.

No timer implementation or scheduling change is justified by this unexecuted
comparison. Keep the existing full-frame controls as the performance evidence.
