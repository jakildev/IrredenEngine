#ifndef GPU_STAGE_TIMING_H
#define GPU_STAGE_TIMING_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace IRRender {

inline constexpr float kFrameTimeBudgetMs = 1000.0f / 60.0f;

struct GpuStageTiming {
    float canvasClearMs_       = 0.0f;
    float voxelCompactMs_      = 0.0f;
    float voxelStage1Ms_       = 0.0f;
    float voxelStage2Ms_       = 0.0f;
    float shapeCompactMs_      = 0.0f;
    float shapePass0Ms_        = 0.0f;
    float shapePass1Ms_        = 0.0f;
    float textToTrixelMs_      = 0.0f;
    float computeVoxelAoMs_    = 0.0f;
    float computeSunShadowMs_  = 0.0f;
    float lightingToTrixelMs_  = 0.0f;
    float trixelToTrixelMs_    = 0.0f;
    float trixelToFbMs_        = 0.0f;
    float entityCanvasToFbMs_  = 0.0f;
    float fbToScreenMs_        = 0.0f;
    std::uint32_t visibleShapeCount_ = 0;
    std::uint32_t shapeGroupsZ_ = 0;
    bool enabled_ = false;
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
    float GpuStageTiming::* field_;
    float budgetShare_;
};

inline const std::array<GpuStageInfo, 15> &gpuStageRegistry() {
    static const std::array<GpuStageInfo, 15> registry{{
        {"canvasClear",       &GpuStageTiming::canvasClearMs_,      0.05f},
        {"voxelCompact",      &GpuStageTiming::voxelCompactMs_,     0.10f},
        {"voxelStage1",       &GpuStageTiming::voxelStage1Ms_,      0.20f},
        {"voxelStage2",       &GpuStageTiming::voxelStage2Ms_,      0.15f},
        {"shapeCompact",      &GpuStageTiming::shapeCompactMs_,     0.05f},
        {"shapePass0",        &GpuStageTiming::shapePass0Ms_,       0.10f},
        {"shapePass1",        &GpuStageTiming::shapePass1Ms_,       0.10f},
        {"textToTrixel",      &GpuStageTiming::textToTrixelMs_,     0.05f},
        {"computeVoxelAO",    &GpuStageTiming::computeVoxelAoMs_,   0.10f},
        {"computeSunShadow",  &GpuStageTiming::computeSunShadowMs_, 0.10f},
        {"lightingToTrixel",  &GpuStageTiming::lightingToTrixelMs_, 0.10f},
        {"trixelToTrixel",    &GpuStageTiming::trixelToTrixelMs_,   0.05f},
        {"trixelToFb",        &GpuStageTiming::trixelToFbMs_,       0.15f},
        {"entityCanvasToFb",  &GpuStageTiming::entityCanvasToFbMs_, 0.05f},
        {"fbToScreen",        &GpuStageTiming::fbToScreenMs_,       0.05f},
    }};
    return registry;
}

inline float budgetMsFor(const GpuStageInfo &info) {
    return kFrameTimeBudgetMs * info.budgetShare_;
}

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_H */
