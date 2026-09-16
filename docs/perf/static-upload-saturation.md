# Upload ownership and queued Metal writes

A saturated position-upload queue now scans the live transform mask and uploads
only contiguous CPU-owned spans. Dropped queue notifications still reach every
static slot, while GPU-transformed positions retain their owner. The existing
canvas-switch path and saturation path share the same helper.

This also exposed a backend ordering defect: orphaning an encoded Metal buffer
used CPU memcpy for untouched bytes, before queued GPU writes had executed.
Aligned partial uploads now copy untouched spans with ordered GPU blits. Earlier
encoders keep the old allocation; subsequent bindings use the new allocation.
Copy destinations enter the encoded set, preventing a later patch from racing
those copies. The blits participate in existing GPU timing scopes.

Full replacements and unencoded uploads retain their fast paths. macOS buffer
blits require four-byte offsets and lengths; the byte-granular API preserves
unaligned partial writes by waiting for queued work and then patching in place.
This rare path deliberately synchronizes. It is not used by aligned voxel
position spans.

## Validation

- The saturated mixed-ownership test failed ten slot checks against the parent
  whole-prefix upload. After static-only uploading, queued GPU-fill coverage
  still failed two GPU-owned slots against the old Metal orphaning code.
- With both repairs, all 21 focused upload/Metal/timestamp tests pass. Coverage
  includes static, mixed and all-GPU pools, notifications dropped after queue
  saturation, consecutive patches in both orders, earlier allocation snapshots,
  pending GPU writes, and byte patches in eight- and nine-byte buffers.
- Full native suite: 1,734 passed; one OpenGL-only test skipped. Builds for tests,
  IRCanvasStress and IRPerfGrid, header/Metal registry checks and comment lint pass.
- Frozen IRCanvasStress pose 0.47, nine yaw angles, six-frame warmup: eight images
  match the parent RGB bytes. The 45-degree image differs only at the known
  12-pixel seam, rectangle [1300,1306) x [588,590). A second run of the identical
  repaired binary flips that same seam back, with all other pixels unchanged.
  This confirms run-to-run instability at that seam; it does not establish its
  cause or fix it. Captures 978–986 and 987–995 retain that comparison locally.
  Representative parent/current screenshots are in
  `docs/pr-screenshots/codex/static-upload-saturation/`.

## Performance and remaining work

Three frozen 64³/yaw-45/zoom-4 Debug Metal runs of 600 frames on Apple M4 Max
average 19.323 ms/frame (19.250–19.360), versus 18.857 (18.830–18.900) for the
preceding batching change. This approximately 2.5% increase is a correctness
cost, not a speedup. Reports and provenance are in
[static-upload-saturation/](static-upload-saturation/). Grouped runs, existing
profiling enabled, no CPU sampling; overflow capacity remains saturated in this
stress workload. GPU stage durations are sampled invocations, not additive
frame totals.

Measure fragmented aligned uploads and consolidate copies without reverting to
unsynchronized CPU reads. The overflow seam and OpenGL runtime validation remain
separate follow-ups.
