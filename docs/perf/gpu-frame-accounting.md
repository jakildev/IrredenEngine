# Completed-frame GPU accounting

Metal reports GPU frame timings from every completed command buffer between
`beginFrame()` and `present()`, independently of the sampled stage timer slots.
Screenshot readback and explicit finish calls can submit multiple buffers in a
single frame. Collection uses existing completion waits; it introduces no GPU
submission or synchronization. Other backends explicitly report unsupported.

- `envelope`: earliest GPU start to latest GPU end. Includes gaps between
  submissions, such as screenshot readback and CPU encoding.
- `commandBufferSpans`: sum of the completed buffers' GPU end minus start.
  Includes GPU stalls; neither metric measures GPU busy time.
- Coverage includes attempted, valid and invalid frames. `commandBuffers` counts
  buffers belonging to valid frames only. Missing, failed or invalid timestamps
  invalidate the whole frame instead of silently reporting a partial duration.

The report and Python tools separate these rows from per-invocation stage
samples. Frame collection follows frame profiling and works with stage profiling
disabled. OpenGL needs an independent frame-query ring; nesting a frame scope in
the existing Metal stage attachment would overwrite stage boundaries.

## Validation

IRPerfGrid, IRCanvasStress and IrredenEngineTest built on Metal Debug. Thirteen
focused tests passed without skips, including four frame-accumulator tests,
three timestamp-poll tests and six native GPU/upload probes. Sixteen Python
profiling tests passed; parser coverage distinguishes legacy missing data from
explicit unsupported data and separates frame metrics from stage samples.

Screenshot submission checks recorded 42 valid frames across 49 command buffers
in IRPerfGrid and 565 valid frames across 574 buffers in CanvasStress, with zero
invalid frames. These capture runs overlapped build activity and are correctness
evidence only. Reports are retained in [gpu-frame-accounting/](gpu-frame-accounting/).
All nine full-turn CanvasStress pairs have identical RGB pixels;
[comparison](gpu-frame-accounting/canvas-comparison.json).

| Parent | Frame accounting (identical RGB) |
|---|---|
| ![Before](../pr-screenshots/codex/gpu-frame-accounting/capture-1233.png) | ![After](../pr-screenshots/codex/gpu-frame-accounting/capture-1242.png) |

## Measurements

Apple M4 Max, Metal Debug, frozen voxel-set wave, yaw45°, zoom4, default lighting
and shadows. Two 300-frame runs per case, including startup. Default uses 64³
entities and pool64; million uses 100³ entities and pool128. No screenshots are
captured during timed runs. Cases run sequentially, not interleaved controls;
small differences do not establish a profiling-overhead speedup.

| Case | Frame mean ms (run range) | GPU envelope / buffer spans mean ms |
|---|---:|---:|
| default-stage-on | 17.670 (17.640–17.700) | 13.299 |
| default-stage-off-no-overlay | 17.025 (17.010–17.040) | 13.035 |
| million-stage-off-no-overlay | 49.930 (49.880–49.980) | 34.664 |
| default-stage-on-no-overlay | 17.395 (17.370–17.420) | 13.206 |

All eight accepted runs have 300/300 valid frames and one command buffer per
frame. Envelope and buffer spans therefore match. Reports and binary/shader
fingerprints are retained alongside each configuration.

The stage-disabled controls also disable the overlay (`--no-overlay`): the
overlay deliberately re-enables stage profiling each frame. A queued attempt
without that flag is excluded; it had stage samples and exceeded the runner's
timeout while waiting for fleet resources. Configuration snapshots accompany
accepted reports; the runtime configuration is restored after measurements.

The million case reports 1,000,175 total entities (one million scene voxels plus
engine/demo entities), about three simulation updates per rendered frame, and
34.7ms of GPU command-buffer duration. This is above the 16.7ms frame target
before CPU costs. Prioritize GPU work under rotation/subdivision, while measuring
repeated update and transform work separately. These measurements establish a
baseline, not a speedup or a promise of million-entity 60fps. Stage-disabled runs
omit timing-gated cull diagnostics; capacity correctness remains covered by the
preceding capacity/sort tests and stage-enabled million controls.
