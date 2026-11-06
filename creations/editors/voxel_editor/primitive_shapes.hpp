#ifndef PRIMITIVE_SHAPES_H
#define PRIMITIVE_SHAPES_H

#include "sculpt_grid.hpp"
#include <irreden/math/sdf.hpp>

namespace IRVoxelEditor {

enum class PrimitiveShape { CUBE, SPHERE, CONE, CYLINDER, PYRAMID };

// Closed unit cells intersect the ideal solid inside the requested voxel box.
// Independent dimensions stretch the sphere/cone/cylinder into elliptical
// cross-sections. Apexes face negative Z. Cell intersection keeps the poles
// of thin and even-sized solids without shifting their centre or extent.
inline bool primitiveShapeCell(PrimitiveShape type, IRMath::ivec3 cell, IRMath::ivec3 size) {
    if (!cellInBounds(cell, size))
        return false;
    const auto half = IRMath::vec3(size) * 0.5f;
    const auto center = IRMath::vec3(cell) + 0.5f - half;
    const auto closest = IRMath::max(IRMath::abs(center) - 0.5f, IRMath::vec3(0)) / half;
    constexpr float kBoundaryEpsilon = 1e-6f;
    switch (type) {
    case PrimitiveShape::CUBE:
        return true;
    case PrimitiveShape::SPHERE:
        return IRMath::SDF::sphere(closest, 1.0f) <= kBoundaryEpsilon;
    case PrimitiveShape::CYLINDER:
        return IRMath::SDF::cylinder(IRMath::vec3(closest.x, closest.y, 0), 1.0f, 1.0f) <=
               kBoundaryEpsilon;
    case PrimitiveShape::CONE: {
        const float bottom = (center.z + 0.5f) / half.z;
        return IRMath::SDF::cone(IRMath::vec3(closest.x, closest.y, -bottom), 1.0f, 1.0f) <=
               kBoundaryEpsilon;
    }
    case PrimitiveShape::PYRAMID: {
        const float radius = (center.z + 0.5f + half.z) / static_cast<float>(size.z);
        return IRMath::SDF::box(
                   IRMath::vec3(closest.x, closest.y, 0),
                   IRMath::vec3(radius, radius, 1)
               ) <= kBoundaryEpsilon;
    }
    }
    return false;
}

} // namespace IRVoxelEditor

#endif
