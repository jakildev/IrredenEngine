# Batched voxel update spans

`UPDATE_VOXEL_SET_CHILDREN` coalesces adjacent spans within each worker’s
pending vector when pool and upload ownership match. Gaps, different pools and
CPU/GPU upload-policy boundaries remain separate. The main-thread merge invokes
`queuePositionRange` for CPU-owned spans (which invalidates cull bounds itself),
and only invalidates bounds for GPU-owned spans.

No position calculation, cull decision, transform state or shader changes.
Containers retain capacity across ticks. This avoids one deferred record and
repeated per-chunk notifications for every entity in a contiguous run without
caching transforms or skipping visible updates.

## Measurement

Native Apple M4 Max, Metal Debug, frozen IRPerfGrid 64³, yaw 45°, zoom 4,
three 600-frame runs per version, CPU sampling disabled. Commands, binary/shader
hashes, CPU/GPU reports and run summaries are in [voxel-update-spans/](voxel-update-spans/).
Runs were grouped before then after; this is a local observation, not a broad
hardware throughput guarantee. Existing scope/GPU profiling remains enabled.

| Measurement | Before mean (run range) | After mean (run range) |
|---|---:|---:|
| Frame ms | 20.367 (20.270–20.430) | 18.857 (18.830–18.900) |
| UpdateVoxelSetChildren ms/call | 1.690 (1.629–1.722) | 0.398 (0.389–0.408) |
| SingleVoxelToCanvasFirst CPU ms/call | 1.453 (1.452–1.454) | 1.464 (1.458–1.474) |

Frame mean fell about 7.4%; update-system cost per call fell about 76.4%.
Updates per 600 frames also fell (729–735 before, 677–680 after), because fixed
simulation catch-up depends on render duration. The per-call comparison isolates
that count difference. GPU work is unchanged; the saturated overflow limitation
of this stress workload remains as documented in [CPU sampling](native-cpu-sampling.md).

## Correctness

- 18 focused native tests pass: contiguous-span boundaries, GPU-owned spans
  excluded from upload while both cull caches’ existing invalidation coverage
  remains, and existing chunk-bound eviction/saturation cases.
- IRCanvasStress frozen pose 0.47, nine angles from 0 to 2π, six-frame screenshot
  warmup: all nine RGB images byte-identical to the retained integrated parent
  control (captures 915–923 versus 969–977). Representative 45° and 225° images
  are in `docs/pr-screenshots/codex/voxel-update-spans/`.
- Header conventions and Metal registries pass. OpenGL runtime remains untested
  on this host.

## Follow-up found during review

The existing saturated upload-queue fallback uploads the entire live pool prefix,
which can overwrite GPU-transform-owned slots in a mixed pool. Batching reduces
how often saturation happens; it does not repair that independent fallback.
Route saturation through the existing static-only upload path and add a mixed-
ownership regression before claiming that boundary is safe.
