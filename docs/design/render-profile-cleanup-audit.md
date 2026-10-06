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

## Next cleanup slices

| Priority | Finding | Bounded change and required proof |
|---|---|---|
| 1 | Invalid scratch source and its quality-tool exception | Delete `systems/copilot_nonesense.cpp` and its explicit exclusion; it defines no usable engine API. Run format/header checks. |
| 2 | Repeated per-axis occupied-cell decoding in five shader pairs | Share integer group flattening, active-count and cell decoding; keep bindings and distinct face spaces explicit. Prove finalize/list coverage and unchanged AO/shadow/lit captures on both backends. |
| 2 | Metal framebuffer readback repeats `finish()` | Reuse the identical submission/wait/accounting sequence; preserve readback validation and keep `present()` separate. Test frame accounting and screenshot bytes. |
| 2 | Canvas destruction repeats backing cleanup | Share only the color/depth/id/Hi-Z release sequence; preserve allocate-before-release and ancillary resources. Run canvas growth/lifecycle tests. |
| 2 | Abandoned resource-manager singleton scaffolding | Remove unused private declarations and commented accessors while retaining the actual manager lifecycle. Run resource-manager tests/header checks. |
| 3 | Lighting-route density patching may be redundant | Audit every consumer before removing UBO patch/restore; preserve density in store/scatter/resolve. Verify cardinal transitions, high density, overflow lighting and fog. |
| 3 | Fallback sun bake retains a raw per-axis branch its driver does not use | Prove route-zero dispatches, then remove the branch and redundant include while preserving ABI and live fallback casting. Exercise detached casting and splat-enabled cases. |
| 3 | Source-face shadow-debug palette repeats an existing helper | Delegate to `surfaceShadowDebugColor` in both shader backends; compare diagnostic captures. |

Shader edits must account for the active store-frame, SDF receiver/normal and fog
work before choosing a base. The initial overlap inventory includes PRs #4135,
#4058, #4040 and #4048. Consolidating integer addressing is distinct from merging
view/model/world-space transforms or changing quantization and binding restoration.

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
