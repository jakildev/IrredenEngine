#ifndef LIGHT_VOLUME_DISPATCH_H
#define LIGHT_VOLUME_DISPATCH_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/render/ir_render_types.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace IRRender::detail {

inline constexpr IRMath::ivec3 kLightPropagationGroupSize{8, 8, 4};

struct LightVolumeDispatch {
    IRMath::ivec3 origin_{0};
    IRMath::ivec3 groups_{0};
};

inline constexpr int kLightVolumeFrameIterationBudget = 4 * kLightVolumePropagateIterations;

struct LightVolumeRadiusMap {
    std::array<int, kLightVolumePropagateIterations + 1> classForRadius_{};
    int classCount_ = 0;
    int totalIterations_ = 0;
};

inline LightVolumeRadiusMap
lightVolumeRadiusMap(const std::array<bool, kLightVolumePropagateIterations + 1> &activeRadii) {
    LightVolumeRadiusMap result;
    std::array<int, kLightVolumePropagateIterations> classes{};
    int classCount = 0;
    for (int radius = kLightVolumePropagateIterations; radius >= 1; --radius) {
        if (activeRadii[radius]) {
            classes[classCount++] = radius;
            result.classForRadius_[radius] = radius;
            result.totalIterations_ += radius;
        }
    }

    while (result.totalIterations_ > kLightVolumeFrameIterationBudget) {
        IR_ASSERT(classCount >= 2, "An over-budget light schedule requires two radius classes");
        int mergeIndex = 0;
        int smallestDifference = classes[0] - classes[1];
        for (int i = 1; i + 1 < classCount; ++i) {
            const int difference = classes[i] - classes[i + 1];
            if (difference < smallestDifference) {
                mergeIndex = i;
                smallestDifference = difference;
            }
        }
        const int wider = classes[mergeIndex];
        const int narrower = classes[mergeIndex + 1];
        for (int radius = 1; radius <= kLightVolumePropagateIterations; ++radius) {
            if (result.classForRadius_[radius] == narrower) {
                result.classForRadius_[radius] = wider;
            }
        }
        result.totalIterations_ -= narrower;
        for (int i = mergeIndex + 1; i + 1 < classCount; ++i) {
            classes[i] = classes[i + 1];
        }
        --classCount;
    }
    result.classCount_ = classCount;
    return result;
}

struct LightVolumePropagationPass {
    std::uint32_t lightOffset_ = 0;
    std::uint32_t lightCount_ = 0;
    int iterations_ = 0;
    float stepFalloff_ = 0.0f;
    LightVolumeDispatch dispatch_{};
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

inline void lightVolumePropagationSchedule(
    std::vector<GPULightSource> &lights,
    IRMath::ivec3 worldOriginVoxel,
    std::vector<GPULightSource> &scratch,
    std::vector<LightVolumePropagationPass> &passes
) {
    passes.clear();
    if (lights.empty()) {
        return;
    }

    std::array<bool, kLightVolumePropagateIterations + 1> activeRadii{};
    for (const auto &light : lights) {
        const int radius = static_cast<int>(light.directionAndRadius_.w);
        IR_ASSERT(
            radius >= 1 && radius <= kLightVolumePropagateIterations,
            "Staged light radius must be inside the propagation cap"
        );
        activeRadii[radius] = true;
    }
    const LightVolumeRadiusMap radiusMap = lightVolumeRadiusMap(activeRadii);
    for (auto &light : lights) {
        const int radius = static_cast<int>(light.directionAndRadius_.w);
        const int classRadius = radiusMap.classForRadius_[radius];
        if (classRadius == radius) {
            continue;
        }
        const IRMath::ivec3 seed = IRMath::ivec3(light.originAndType_);
        const IRMath::ivec3 apex = IRMath::ivec3(light.trueOriginVoxel_);
        const IRMath::ivec3 delta = seed - apex;
        const int boundaryDistance =
            IRMath::abs(delta.x) + IRMath::abs(delta.y) + IRMath::abs(delta.z);
        light.directionAndRadius_.w = static_cast<float>(classRadius);
        light.coneAndSeedAlpha_.y =
            1.0f - static_cast<float>(boundaryDistance) / static_cast<float>(classRadius);
    }

    std::array<std::uint32_t, kLightVolumePropagateIterations + 1> counts{};
    int maxRadius = 0;
    for (const auto &light : lights) {
        const int radius = static_cast<int>(light.directionAndRadius_.w);
        IR_ASSERT(
            radius >= 1 && radius <= kLightVolumePropagateIterations,
            "Staged light class radius must be inside the propagation cap"
        );
        ++counts[radius];
        maxRadius = IRMath::max(maxRadius, radius);
    }

    int classCount = 0;
    for (int radius = maxRadius; radius >= 1; --radius) {
        classCount += counts[radius] > 0 ? 1 : 0;
    }
    const int dispatchReach = maxRadius + (classCount > 1 ? 1 : 0);
    const LightVolumeDispatch dispatch =
        lightVolumePropagationDispatch(lights, worldOriginVoxel, dispatchReach);

    scratch.resize(lights.size());
    std::array<std::size_t, kLightVolumePropagateIterations + 1> next{};
    std::size_t offset = 0;
    for (int radius = maxRadius; radius >= 1; --radius) {
        next[radius] = offset;
        offset += counts[radius];
    }
    for (const auto &light : lights) {
        const int radius = static_cast<int>(light.directionAndRadius_.w);
        scratch[next[radius]++] = light;
    }
    lights.swap(scratch);

    std::uint32_t lightOffset = 0;
    for (int radius = maxRadius; radius >= 1; --radius) {
        if (counts[radius] == 0) {
            continue;
        }
        passes.push_back(
            {lightOffset, counts[radius], radius, 1.0f / static_cast<float>(radius), dispatch}
        );
        lightOffset += counts[radius];
    }
}

} // namespace IRRender::detail

#endif
