#ifndef GPU_STAGE_TIMING_H
#define GPU_STAGE_TIMING_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

#include <irreden/ir_math.hpp>

namespace IRRender {

inline constexpr float kFrameTimeBudgetMs = 1000.0f / 60.0f;

// Number of named GPU stages in `gpuStageRegistry()`. Single source of truth
// for both the registry array and the parallel per-stage accumulator array.
inline constexpr std::size_t kGpuStageCount = 20;

struct GpuStageTiming {
    float canvasClearMs_ = 0.0f;
    float voxelCompactMs_ = 0.0f;
    float voxelStage1Ms_ = 0.0f;
    float voxelStage2Ms_ = 0.0f;
    float shapeCompactMs_ = 0.0f;
    float shapePass0Ms_ = 0.0f;
    float shapePass1Ms_ = 0.0f;
    float textToTrixelMs_ = 0.0f;
    float buildLightOcclusionGridMs_ = 0.0f;
    float computeVoxelAoMs_ = 0.0f;
    float bakeSunShadowMapMs_ = 0.0f;
    float computeSunShadowMs_ = 0.0f;
    float computeLightVolumeMs_ = 0.0f;
    float lightingToTrixelMs_ = 0.0f;
    float fogToTrixelMs_ = 0.0f;
    float trixelToTrixelMs_ = 0.0f;
    float trixelToFbMs_ = 0.0f;
    float entityCanvasToFbMs_ = 0.0f;
    float resolvePerAxisScreenDepthMs_ = 0.0f;
    float fbToScreenMs_ = 0.0f;
    std::uint32_t visibleShapeCount_ = 0;
    std::uint32_t shapeGroupsZ_ = 0;
    // Cull diagnostic. Populated by VOXEL_TO_TRIXEL_STAGE_1 each frame
    // from the prior frame's indirect-dispatch params + the current
    // pool live count, so the readback is sync-free (frame N+1 reads
    // frame N's already-written value before zeroing the buffer).
    // Reports the *last* sampled frame; running averages live in
    // VoxelCullAccumulator below.
    std::uint32_t visibleVoxelCount_ = 0;
    std::uint32_t totalVoxelCount_ = 0;
    // Only flipped between frames by Lua on the main thread (Lua runs in
    // INPUT/UPDATE, never RENDER). Stable across the RENDER pipeline, so
    // probes can read `enabled_` twice and rely on both values matching.
    bool enabled_ = false;
    // Development fallback that preserves the old finish()-bracketed timing
    // path. Keep this off for throughput runs; use it only for A/B checks.
    bool legacyFinishTiming_ = false;
};

inline GpuStageTiming &gpuStageTiming() {
    static GpuStageTiming instance;
    return instance;
}

using SteadyClock = std::chrono::steady_clock;
using TimePoint = SteadyClock::time_point;

inline float elapsedMs(TimePoint start, TimePoint end) {
    return std::chrono::duration<float, std::milli>(end - start).count();
}

// `budgetShare_` values do not sum to 1.0 — they are per-pass soft
// budgets reflecting the relative weight of each pass at current
// scales. A pass exceeding its share is flagged as over-budget; the
// absolute 16.67 ms frame limit is a separate top-line check.
struct GpuStageInfo {
    std::string_view name_;
    float GpuStageTiming::*field_;
    float budgetShare_;
};

struct CpuPhaseTiming {
    double totalMs_ = 0.0;
    double maxMs_ = 0.0;
    std::uint32_t sampleCount_ = 0;

    void record(double ms) {
        totalMs_ += ms;
        maxMs_ = IRMath::max(maxMs_, ms);
        ++sampleCount_;
    }

    void reset() {
        totalMs_ = 0.0;
        maxMs_ = 0.0;
        sampleCount_ = 0;
    }
};

struct ComputeLightVolumeTiming {
    CpuPhaseTiming clear_;
    CpuPhaseTiming populate_;
    CpuPhaseTiming upload_;

    void reset() {
        clear_.reset();
        populate_.reset();
        upload_.reset();
    }
};

inline ComputeLightVolumeTiming &computeLightVolumeTiming() {
    static ComputeLightVolumeTiming instance;
    return instance;
}

// Running cull-effectiveness accumulator. Each per-frame sample comes
// from VOXEL_TO_TRIXEL_STAGE_1 reading the prior frame's indirect
// dispatch params. The world's profile-report builder drains this at
// shutdown; `enableFrameTiming(true)` calls reset() so each measurement
// run starts from zero.
struct VoxelCullAccumulator {
    std::uint64_t visibleSum_ = 0;
    std::uint64_t totalSum_ = 0;
    std::uint32_t maxVisible_ = 0;
    std::uint32_t maxTotal_ = 0;
    std::uint32_t sampleCount_ = 0;

    void record(std::uint32_t visible, std::uint32_t total) {
        visibleSum_ += visible;
        totalSum_ += total;
        maxVisible_ = IRMath::max(maxVisible_, visible);
        maxTotal_ = IRMath::max(maxTotal_, total);
        ++sampleCount_;
    }

    void reset() {
        visibleSum_ = 0;
        totalSum_ = 0;
        maxVisible_ = 0;
        maxTotal_ = 0;
        sampleCount_ = 0;
    }
};

inline VoxelCullAccumulator &voxelCullAccumulator() {
    static VoxelCullAccumulator instance;
    return instance;
}

// Per-stage running GPU-timing accumulator. The `gpu_stage_timing_observer`
// records one sample per resolved timestamp pair per stage; the world's
// profile-report builder drains the array (indexed parallel to
// `gpuStageRegistry()` order) at shutdown for true avg / min / max across the
// run. The single `GpuStageTiming::*Ms_` field only ever holds the *last*
// frame's sample, so without this accumulator the report can only echo that
// one value — which is why every stage previously reported Avg == Max (#1738).
// `enableFrameTiming(true)` calls `resetGpuStageAccumulators()` so each
// measurement run starts from zero.
struct GpuStageAccumulator {
    double sumMs_ = 0.0;
    float maxMs_ = 0.0f;
    float minMs_ = 0.0f;
    std::uint32_t sampleCount_ = 0;

    void record(float ms) {
        sumMs_ += ms;
        maxMs_ = IRMath::max(maxMs_, ms);
        minMs_ = sampleCount_ == 0 ? ms : IRMath::min(minMs_, ms);
        ++sampleCount_;
    }

    void reset() {
        sumMs_ = 0.0;
        maxMs_ = 0.0f;
        minMs_ = 0.0f;
        sampleCount_ = 0;
    }
};

inline std::array<GpuStageAccumulator, kGpuStageCount> &gpuStageAccumulators() {
    static std::array<GpuStageAccumulator, kGpuStageCount> instance{};
    return instance;
}

inline void resetGpuStageAccumulators() {
    for (auto &acc : gpuStageAccumulators()) {
        acc.reset();
    }
}

// Commit one resolved GPU-timing sample for a registry stage: overwrite the
// live last-sample field the perf overlay reads, and feed the running
// accumulator the shutdown profile report drains. Shared by the per-system
// `GpuStageTimingObserver` and the intra-tick `GpuSubStageScope` so both write
// samples through one code path. `registryIndex` is the stage's slot in
// `gpuStageRegistry()` order (== its `gpuStageAccumulators()` index).
inline void commitGpuStageSample(const GpuStageInfo &info, int registryIndex, float ms) {
    gpuStageTiming().*(info.field_) = ms;
    if (registryIndex >= 0 &&
        static_cast<std::size_t>(registryIndex) < gpuStageAccumulators().size()) {
        gpuStageAccumulators()[registryIndex].record(ms);
    }
}

// Authoritative mapping name → field → budget. `gpu_stage_timing_observer`
// resolves a system's tag against this table; pass the same name to
// `IRRender::tagGpuStage(system, "<name>")` and the observer fills the
// matching `GpuStageTiming::*Ms_` field at end-of-tick.
//
// Per-system mapping (one `GpuStageTimingObserver` fire per `SystemId`):
//   `shapePass1`   ← SHAPES_TO_TRIXEL (covers former shapePass0 + shapePass1)
//   Most remaining names map 1:1 to single-stage systems.
//
// Intra-tick sub-stage rows (#2280): VOXEL_TO_TRIXEL_STAGE_1 is NOT tagged for
// the per-system observer. Instead its per-canvas tick brackets each of its
// four dispatch groups with a `GpuSubStageScope` (gpu_substage_timing.hpp),
// so these rows are attributed individually rather than bundled:
//   `canvasClear`  ← the per-frame distance-texture clear (blit)
//   `voxelCompact` ← the visibility-compaction dispatch
//   `voxelStage1`  ← the stage-1 raster dispatch ONLY (was the whole-tick
//                    bundle before #2280 wired the sub-rows)
//   `voxelStage2`  ← the stage-2 dispatch (runs inside STAGE_1's tick)
// The old bundled `voxelStage1` value is reconstructed as the sum of these
// four rows. Sub-scopes are single-canvas-exact and record the last canvas's
// sample on multi-canvas scenes (like every `*Ms_` field's last-sample
// semantics); the rotating-only per-axis voxel dispatch is not sub-scoped.
//
// Two rows still have no current writer: `shapePass0` (folded into the
// SHAPES_TO_TRIXEL per-system measurement) and `shapeCompact` (no system has
// ever written it; reserved for a future shape-compaction pass). They stay in
// the registry to keep the Lua API and perf overlay stable — the overlay still
// shows them at 0.0f; the shutdown profile report omits them (sampleCount_ == 0).
inline const std::array<GpuStageInfo, kGpuStageCount> &gpuStageRegistry() {
    static const std::array<GpuStageInfo, kGpuStageCount> registry{{
        {"canvasClear", &GpuStageTiming::canvasClearMs_, 0.05f},
        {"voxelCompact", &GpuStageTiming::voxelCompactMs_, 0.10f},
        {"voxelStage1", &GpuStageTiming::voxelStage1Ms_, 0.20f},
        {"voxelStage2", &GpuStageTiming::voxelStage2Ms_, 0.15f},
        {"shapeCompact", &GpuStageTiming::shapeCompactMs_, 0.05f},
        {"shapePass0", &GpuStageTiming::shapePass0Ms_, 0.10f},
        {"shapePass1", &GpuStageTiming::shapePass1Ms_, 0.10f},
        {"textToTrixel", &GpuStageTiming::textToTrixelMs_, 0.05f},
        {"buildLightOcclusionGrid", &GpuStageTiming::buildLightOcclusionGridMs_, 0.10f},
        {"computeVoxelAO", &GpuStageTiming::computeVoxelAoMs_, 0.10f},
        {"bakeSunShadowMap", &GpuStageTiming::bakeSunShadowMapMs_, 0.10f},
        {"computeSunShadow", &GpuStageTiming::computeSunShadowMs_, 0.10f},
        {"computeLightVolume", &GpuStageTiming::computeLightVolumeMs_, 0.10f},
        {"lightingToTrixel", &GpuStageTiming::lightingToTrixelMs_, 0.10f},
        {"fogToTrixel", &GpuStageTiming::fogToTrixelMs_, 0.05f},
        {"trixelToTrixel", &GpuStageTiming::trixelToTrixelMs_, 0.05f},
        {"trixelToFb", &GpuStageTiming::trixelToFbMs_, 0.15f},
        {"entityCanvasToFb", &GpuStageTiming::entityCanvasToFbMs_, 0.05f},
        {"resolvePerAxisScreenDepth", &GpuStageTiming::resolvePerAxisScreenDepthMs_, 0.05f},
        {"fbToScreen", &GpuStageTiming::fbToScreenMs_, 0.05f},
    }};
    return registry;
}

inline float budgetMsFor(const GpuStageInfo &info) {
    return kFrameTimeBudgetMs * info.budgetShare_;
}

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_H */
