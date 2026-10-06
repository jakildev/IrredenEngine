# Rendering and profiling cleanup audit

First pass against master `e1e5b98a3`, after the bounded-light and timing-control
stack merged. Scope: rendering backend/infrastructure, voxel and lighting shader
families, timing collectors, and profiling scripts. Priorities favor shared
contracts with observable behavior over broad renames or a new rendering layer.

## Implemented slice: timestamp bookkeeping

`detail::GpuTimestampRing` owns one implementation of allocation, asynchronous
polling, round-robin reuse and timestamp writes. Both whole-system and substage
timers use it. Pending pairs stay occupied, invalid pairs become reusable without
recording a zero, and completed pairs contribute one sample. The helper adds no
GPU waits and leaves scope boundaries, CPU timing and legacy finish timing intact.
Handle lifetime remains a caller policy: observers release through their device;
the process-lifetime substage timer performs no GPU calls at static destruction.

The accompanying comments distinguish latest resolved invocation from frame total,
and absent samples from zero duration. The [timing contract](gpu-stage-timing-cost-model.md)
remains the canonical interpretation of those rows.

## Implemented slice: complete pooled profiling evidence

The million-control summary reports pooled steady mean and tail percentiles only
when every included report supplies a valid steady series. A missing series marks
that statistic unavailable instead of silently excluding a potentially slower run.
Complete reports retain the same per-report warmup exclusion and pooling. A
mixed complete/missing regression test fails on the previous implementation.

## Implemented slice: renderer resource cleanup

Canvas destruction and backing growth share the texture/Hi-Z release sequence;
growth still allocates replacements first and preserves ancillary buffers. Metal
framebuffer readback delegates its existing synchronous submission to `finish()`,
retaining validation before submission and completed-frame accounting before the
command buffer is renewed. Presentation remains a separate operation.

The resource manager's unused private singleton declarations and commented
accessors are removed. The uncompiled scratch file
`systems/copilot_nonesense.cpp` and its quality-tool exclusion are removed too;
neither provided an engine API.

## Implemented slice: per-axis integer addressing

The AO, sun-shadow, lighting, fog and depth-resolve shader pairs share
`ir_per_axis_cell_dispatch`: workgroup-row flattening and linear-cell pixel
decoding. The finalizer shares their argument offset and tile size. Bounds checks
remain before compacted-list reads, and each wrapper retains its bindings and
route-specific coordinate recovery.

`test_render_per_axis_cell_dispatch.py` executes both helpers through a C++
adapter, enumerates multirow dispatches beyond 65,535 workgroups, checks row-major
pixels and CPU/shader constants, and rejects lost-row/wrong-column mutations.
Native shader execution and captures remain necessary to cover compilation,
binding and rendering behavior. This extraction preserves the merged store-frame
work and does not consolidate distinct face coordinate spaces.

## Next cleanup slices

| Priority | Finding | Bounded change and required proof |
|---|---|---|
| 3 | Lighting-route density patching may be redundant | Audit every consumer before removing UBO patch/restore; preserve density in store/scatter/resolve. Verify cardinal transitions, high density, overflow lighting and fog. |
| 3 | Fallback sun bake retains a raw per-axis branch its driver does not use | Prove route-zero dispatches, then remove the branch and redundant include while preserving ABI and live fallback casting. Exercise detached casting and splat-enabled cases. |
| 3 | Source-face shadow-debug palette repeats an existing helper | Delegate to `surfaceShadowDebugColor` in both shader backends; compare diagnostic captures. |

Shader edits must account for SDF receiver/normal work before choosing a base.
The per-axis cleanup includes merged PRs #4135 and #4048; open PRs #4058 and
#4040 still overlap its shader paths. Consolidating integer addressing is
distinct from merging view/model/world-space transforms or changing quantization
and binding restoration.

## Lifecycle investigations requiring their own fixes

- The observer registration cache retains a pointer after its SystemManager dies;
  repeated stage registration can replace allocated handles without releasing them.
  Test repeated registration and multiple Worlds before changing ownership.
- Metal texture clears have two pattern-buffer caches with different atomic-scratch
  and encoder-timing behavior. Define queued pattern snapshot/order semantics and
  test changing patterns before consolidating them.
- Shared profiling matrix helpers live in the rotation CLI module. A neutral
  module could clarify ownership once needed; preserve the distinct evidence
  requirements of historical summaries and strict timing controls.

The timing-attribution experiment remains a separate measurement task. Cleanup
does not establish a dense-scene speedup, fix inherited visual artifacts, or
qualify million-entity throughput.
