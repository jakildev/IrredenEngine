#ifndef LIGHT_VOLUME_DISPATCH_H
#define LIGHT_VOLUME_DISPATCH_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <span>

namespace IRRender::detail {

inline constexpr IRMath::ivec3 kLightPropagationGroupSize{8, 8, 4};

struct LightVolumeDispatch {
    IRMath::ivec3 origin_{0};
    IRMath::ivec3 groups_{0};
};

inline LightVolumeDispatch lightVolumePropagationDispatch(
    std::span<const GPULightSource> lights, IRMath::ivec3 worldOriginVoxel, int iterations
) {
    constexpr int gridSize = LightVolumeParams{}.gridSize_;
    constexpr int halfExtent = LightVolumeParams{}.halfExtent_;
    IR_ASSERT(iterations > 0, "Light propagation requires a positive iteration count");
    if (lights.empty()) {
        return {};
    }

    IRMath::ivec3 lower{gridSize};
    IRMath::ivec3 upper{0};
    for (const auto &light : lights) {
        // The staged seed may be clamped or relocated; the true light apex
        // does not describe which texel the seed shader actually writes.
        const IRMath::ivec3 cell =
            IRMath::ivec3(light.originAndType_) - worldOriginVoxel + IRMath::ivec3(halfExtent);
        lower = IRMath::min(lower, cell);
        upper = IRMath::max(upper, cell);
    }

    // Each six-neighbor iteration can move support by at most one cell.
    // Retain the full iteration radius even when RGBA8 alpha quantizes to zero:
    // source RGB and winning IDs are still stored at those seed cells.
    lower =
        IRMath::clamp(lower - IRMath::ivec3(iterations), IRMath::ivec3(0), IRMath::ivec3(gridSize));
    upper = IRMath::clamp(
        upper + IRMath::ivec3(iterations + 1),
        IRMath::ivec3(0),
        IRMath::ivec3(gridSize)
    );
    LightVolumeDispatch dispatch;
    for (int axis = 0; axis < 3; ++axis) {
        const int groupSize = kLightPropagationGroupSize[axis];
        dispatch.origin_[axis] = (lower[axis] / groupSize) * groupSize;
        dispatch.groups_[axis] = IRMath::divCeil(upper[axis] - dispatch.origin_[axis], groupSize);
    }
    return dispatch;
}

} // namespace IRRender::detail

#endif
