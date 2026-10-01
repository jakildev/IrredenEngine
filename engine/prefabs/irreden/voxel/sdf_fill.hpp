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

} // namespace detail

template <typename Edit>
inline void fillSdfRaw(
    IRComponents::C_VoxelSetNew &set,
    IRMath::SDF::ShapeType primitive,
    IRMath::vec4 params,
    IRMath::Color color,
    bool place,
    Edit &&edit
) {
    const std::size_t total = static_cast<std::size_t>(set.size_.x) *
                              static_cast<std::size_t>(set.size_.y) *
                              static_cast<std::size_t>(set.size_.z);
    std::vector<float> distances(total);
    IRMath::SDF::evaluateGrid(set.size_, primitive, params, distances);

    IRMath::iterateAABB({0, 0, 0}, set.size_ - IRMath::ivec3(1), [&](int x, int y, int z) {
        const IRMath::ivec3 local{x, y, z};
        const std::size_t flat =
            static_cast<std::size_t>(IRMath::index3DtoIndex1D(local, set.size_));
        if (distances[flat] <= IRMath::SDF::kSurfaceThreshold) {
            edit(local, flat, place, color);
        }
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
                records[flat].color_ = fillColor;
                records[flat].activate();
            } else {
                records[flat].deactivate();
            }
        }
    );
    set.resyncAfterRawEdits();
}

} // namespace IRPrefab::Voxel

#endif /* IR_PREFAB_VOXEL_SDF_FILL_H */
