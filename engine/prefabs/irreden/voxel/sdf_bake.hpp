#ifndef SDF_BAKE_H
#define SDF_BAKE_H

#include <irreden/voxel/dense_bridge.hpp>
#include <irreden/voxel/sdf_fill.hpp>

#include <limits>

namespace IRPrefab::Voxel {

// Author-time conversion to an ordinary, opaque DENSE asset. Params use the
// descriptor convention (BOX full dimensions); fillSdf instead takes
// evaluation parameters. The explicit grid clips the solid and fixes its phase:
// cell centers are i-(size-1)/2, with the shared 0.5 surface threshold.
// No transforms, descriptor flags, canvas, or pool allocation are baked here.
// Invalid extents, nonfinite params and unregistered CUSTOM_SDF return empty.
inline IRAsset::DenseVoxelSet bakeSdfGrid(
    IRMath::ivec3 size, IRMath::SDF::ShapeType primitive, IRMath::vec4 params, IRMath::Color color
) {
    using namespace IRMath;
    const auto type = static_cast<std::uint32_t>(primitive);
    if (type > static_cast<std::uint32_t>(SDF::ShapeType::TORUS) ||
        primitive == SDF::ShapeType::CUSTOM_SDF) {
        return {};
    }
    for (int axis = 0; axis < 4; ++axis) {
        if (!isFinite(params[axis])) {
            return {};
        }
    }
    std::size_t count = 1;
    for (int axis = 0; axis < 3; ++axis) {
        if (size[axis] <= 0 ||
            static_cast<std::size_t>(size[axis]) >
                static_cast<std::size_t>(std::numeric_limits<int>::max()) / count) {
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
