#ifndef ARRAY_TRANSFORMS_H
#define ARRAY_TRANSFORMS_H

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/ir_math.hpp>

#include <vector>

namespace IRPrefab::Arrays {

inline std::vector<IRComponents::C_LocalTransform>
radial(int count, IRMath::vec3 axis, float radius, float perCopyRotation = 0.0f) {
    std::vector<IRComponents::C_LocalTransform> transforms;
    if (count <= 0 || IRMath::length(axis) == 0.0f) {
        return transforms;
    }

    axis = IRMath::normalize(axis);
    IRMath::vec3 basisU;
    IRMath::vec3 basisV;
    IRMath::buildOrthonormalBasis(axis, basisU, basisV);
    transforms.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float fraction = static_cast<float>(i) / static_cast<float>(count);
        const float angle = fraction * IRMath::kTwoPi;
        const IRMath::vec3 translation =
            radius * (basisU * IRMath::cos(angle) + basisV * IRMath::sin(angle));
        transforms.emplace_back(
            translation,
            IRMath::quatAxisAngle(axis, static_cast<float>(i) * perCopyRotation)
        );
    }
    return transforms;
}

inline std::vector<IRComponents::C_LocalTransform> linear(int count, IRMath::vec3 step) {
    std::vector<IRComponents::C_LocalTransform> transforms;
    if (count <= 0) {
        return transforms;
    }
    transforms.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        transforms.emplace_back(static_cast<float>(i) * step);
    }
    return transforms;
}

} // namespace IRPrefab::Arrays

#endif /* ARRAY_TRANSFORMS_H */
