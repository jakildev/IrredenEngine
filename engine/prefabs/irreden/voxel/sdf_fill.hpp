#ifndef IR_PREFAB_VOXEL_SDF_FILL_H
#define IR_PREFAB_VOXEL_SDF_FILL_H

#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace IRPrefab::Voxel {
namespace detail {

inline std::span<IRComponents::C_Voxel> editableRecords(IRComponents::C_VoxelSetNew &set) {
    if (!set.pendingVoxels_.empty()) {
        return set.pendingVoxels_;
    }
    return set.voxels_;
}

inline void placeVoxel(IRComponents::C_Voxel &voxel, IRMath::Color color) {
    voxel.color_ = color;
    voxel.activate();
}

} // namespace detail

// Calls visit(local, flat) for every cell of a @p size grid the primitive
// covers. This is the cell set every fill below writes, so a caller that
// models a fill without a voxel set walks it instead of re-deriving it.
template <typename Visit>
inline void forEachSdfGridCell(
    IRMath::ivec3 size, IRMath::SDF::ShapeType primitive, IRMath::vec4 params, Visit &&visit
) {
    const std::size_t total = static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
                              static_cast<std::size_t>(size.z);
    std::vector<float> distances(total);
    IRMath::SDF::evaluateGrid(size, primitive, params, distances);

    IRMath::iterateAABB({0, 0, 0}, size - IRMath::ivec3(1), [&](int x, int y, int z) {
        const IRMath::ivec3 local{x, y, z};
        const std::size_t flat = static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, size));
        if (distances[flat] <= IRMath::SDF::kSurfaceThreshold) {
            visit(local, flat);
        }
    });
}

template <typename Edit>
inline void fillSdfRaw(
    IRComponents::C_VoxelSetNew &set,
    IRMath::SDF::ShapeType primitive,
    IRMath::vec4 params,
    IRMath::Color color,
    bool place,
    Edit &&edit
) {
    forEachSdfGridCell(set.size_, primitive, params, [&](IRMath::ivec3 local, std::size_t flat) {
        edit(local, flat, place, color);
    });
}

template <typename Edit>
inline void fillSdf(
    IRComponents::C_VoxelSetNew &set,
    IRMath::SDF::ShapeType primitive,
    IRMath::vec4 params,
    IRMath::Color color,
    bool place,
    Edit &&edit
) {
    fillSdfRaw(set, primitive, params, color, place, std::forward<Edit>(edit));
    set.resyncAfterRawEdits();
}

inline void fillSdf(
    IRComponents::C_VoxelSetNew &set,
    IRMath::SDF::ShapeType primitive,
    IRMath::vec4 params,
    IRMath::Color color,
    bool place = true
) {
    auto records = detail::editableRecords(set);
    fillSdfRaw(
        set,
        primitive,
        params,
        color,
        place,
        [&](IRMath::ivec3, std::size_t flat, bool shouldPlace, IRMath::Color fillColor) {
            if (flat >= records.size()) {
                return;
            }
            if (shouldPlace) {
                detail::placeVoxel(records[flat], fillColor);
            } else {
                records[flat].deactivate();
            }
        }
    );
    set.resyncAfterRawEdits();
}

} // namespace IRPrefab::Voxel

#endif /* IR_PREFAB_VOXEL_SDF_FILL_H */
