#ifndef SDF_BAKE_H
#define SDF_BAKE_H

#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/voxel/sdf_fill.hpp>

#include <limits>

namespace IRPrefab::Voxel {

// Distance scratch forEachSdfGridCell holds alongside the output records while
// bakeSdfGrid runs over @p cells grid cells. It is freed before the bake returns.
constexpr std::size_t sdfBakeScratchBytes(std::size_t cells) {
    return cells * sizeof(float);
}

// Author-time conversion to an ordinary, opaque DENSE asset. Params use the
// descriptor convention (BOX full dimensions); fillSdf instead takes
// evaluation parameters. The explicit grid clips the solid and fixes its phase:
// cell centers are i-(size-1)/2, with the shared 0.5 surface threshold.
// No transforms, descriptor flags, canvas, or pool allocation are baked here.
// maxWorkingBytes bounds voxel-record plus distance-scratch payload, excluding
// allocator overhead and later pool uploads. Over-budget or invalid inputs
// return empty before allocating; allocation failure still propagates normally.
inline IRAsset::DenseVoxelSet bakeSdfGrid(
    IRMath::ivec3 size,
    IRMath::SDF::ShapeType primitive,
    IRMath::vec4 params,
    IRMath::Color color,
    std::size_t maxWorkingBytes = 256u * 1024u * 1024u
) {
    using namespace IRMath;
    switch (primitive) {
    case SDF::ShapeType::BOX:
    case SDF::ShapeType::SPHERE:
    case SDF::ShapeType::CYLINDER:
    case SDF::ShapeType::ELLIPSOID:
    case SDF::ShapeType::CURVED_PANEL:
    case SDF::ShapeType::WEDGE:
    case SDF::ShapeType::TAPERED_BOX:
    case SDF::ShapeType::CONE:
    case SDF::ShapeType::TORUS:
        break;
    case SDF::ShapeType::CUSTOM_SDF:
    default:
        return {};
    }
    for (int axis = 0; axis < 4; ++axis) {
        if (!isFinite(params[axis])) {
            return {};
        }
    }
    constexpr std::size_t bytesPerCell = sizeof(IRAsset::VoxelRecord) + sdfBakeScratchBytes(1);
    const auto maxCells =
        min(maxWorkingBytes / bytesPerCell,
            static_cast<std::size_t>(std::numeric_limits<int>::max()));
    std::size_t count = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (size[axis] <= 0 || static_cast<std::size_t>(size[axis]) > maxCells / count) {
            return {};
        }
        count *= static_cast<std::size_t>(size[axis]);
    }
    IRAsset::DenseVoxelSet dense;
    dense.boundsMin_ = ivec3(0);
    dense.boundsMax_ = size;
    dense.voxels_.resize(count);
    color.alpha_ = 255;
    const auto filled = IRPrefab::DenseVoxel::toRecord(IRComponents::C_Voxel{color});
    forEachSdfGridCell(
        size,
        primitive,
        SDF::effectiveParams(primitive, params),
        [&](ivec3, std::size_t flat) { dense.voxels_[flat] = filled; }
    );
    return dense;
}

} // namespace IRPrefab::Voxel

#endif /* SDF_BAKE_H */
