#ifndef GPU_STAGE_TIMING_H
#define GPU_STAGE_TIMING_H

#include <chrono>
#include <cstdint>

namespace IRRender {

struct GpuStageTiming {
    float canvasClearMs_ = 0.0f;
    float voxelCompactMs_ = 0.0f;
    float voxelStage1Ms_ = 0.0f;
    float voxelStage2Ms_ = 0.0f;
    float shapeCompactMs_ = 0.0f;
    float shapePass0Ms_ = 0.0f;
    float shapePass1Ms_ = 0.0f;
    float trixelToFbMs_ = 0.0f;
    float entityCanvasToFbMs_ = 0.0f;
    float fbToScreenMs_ = 0.0f;
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

} // namespace IRRender

#endif /* GPU_STAGE_TIMING_H */
