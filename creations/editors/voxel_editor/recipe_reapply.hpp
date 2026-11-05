#ifndef IR_VOXEL_EDITOR_RECIPE_REAPPLY_H
#define IR_VOXEL_EDITOR_RECIPE_REAPPLY_H

#include "module_session_view.hpp"

#include <irreden/voxel/components/component_voxel.hpp>

#include <map>
#include <optional>
#include <span>
#include <vector>

namespace IRVoxelEditor {

struct RecipeReapplyEdit {
    IRMath::ivec3 local_;
    std::size_t flat_;
    bool place_;
    IRMath::Color color_;
};

// Only default authored metadata is recognized as generated; nondefault layer,
// material, bone and shading values are conservatively preserved. Face occupancy
// and rotation/fog carriers are derived rendering state.
inline bool matchesGeneratedRecipeCell(const IRComponents::C_Voxel &live, IRMath::Color generated) {
    generated.alpha_ = 255;
    return live.color_.toPackedRGBA() == generated.toPackedRGBA() && live.material_id_ == 0 &&
           live.bone_id_ == 0 && live.layer_id_ == 0 &&
           (live.flags_ & ~IRComponents::VoxelFlags::kFaceOccludedMask) ==
               IRComponents::VoxelFlags::kAoContrib &&
           (live.reserved_ & IRComponents::VoxelReserved::kPriorityMask) == 0;
}

// Both evaluated outputs must already include the binding's persistent class
// mask. Compare against authored records, never a rotated raster destination.
// Changed/erased old cells and occupied new-only cells are manual overrides.
// This comparison cannot retain an erased-cell tombstone after its coordinate
// leaves the generated footprint; a later expansion may fill that cell again.
inline std::optional<std::vector<RecipeReapplyEdit>> planRecipeReapply(
    std::span<const RecipeCell> oldCells,
    std::span<const RecipeCell> newCells,
    std::span<const IRComponents::C_Voxel> authored,
    IRMath::ivec3 size,
    IRMath::Color oldFallback,
    IRMath::Color newFallback
) {
    if (size.x <= 0 || size.y <= 0 || size.z <= 0 ||
        authored.size() != static_cast<std::size_t>(size.x) * size.y * size.z)
        return std::nullopt;

    struct Change {
        IRMath::ivec3 local_;
        std::optional<IRMath::Color> old_;
        std::optional<IRMath::Color> next_;
    };
    std::map<std::size_t, Change> changes;
    const auto collect = [&](std::span<const RecipeCell> cells, IRMath::Color fallback, bool old) {
        for (const RecipeCell &cell : cells) {
            const auto p = cell.local_;
            if (p.x < 0 || p.y < 0 || p.z < 0 || p.x >= size.x || p.y >= size.y || p.z >= size.z)
                continue;
            const auto flat = static_cast<std::size_t>(IRMath::index3DtoIndex1D(p, size));
            Change &change = changes[flat];
            change.local_ = p;
            IRMath::Color color = cell.color_.value_or(fallback);
            color.alpha_ = 255;
            (old ? change.old_ : change.next_) = color;
        }
    };
    collect(oldCells, oldFallback, true);
    collect(newCells, newFallback, false);

    std::vector<RecipeReapplyEdit> edits;
    for (const auto &[flat, change] : changes) {
        const auto &live = authored[flat];
        if (change.old_ ? !matchesGeneratedRecipeCell(live, *change.old_) : live.color_.alpha_ != 0)
            continue;
        if (!change.next_) {
            edits.push_back({change.local_, flat, false, oldFallback});
        } else if (live.color_.toPackedRGBA() != change.next_->toPackedRGBA()) {
            edits.push_back({change.local_, flat, true, *change.next_});
        }
    }
    return edits;
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_RECIPE_REAPPLY_H */
