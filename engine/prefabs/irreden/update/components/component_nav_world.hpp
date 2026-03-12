#ifndef COMPONENT_NAV_WORLD_H
#define COMPONENT_NAV_WORLD_H

#include <irreden/ir_math.hpp>

namespace IRComponents {

/// Navigation world config. Nav resolution is decoupled from voxel resolution.
/// Industry pattern: 1 nav cell = N voxels (typically 4–16).
/// Sweet spot: nav_cell ≈ player_radius_voxels / 2.
struct C_NavWorld {
    IRMath::vec3 origin_;
    float cellSizeWorld_;       ///< World units per nav cell
    IRMath::ivec3 chunkSize_;
    bool dirty_{true};          ///< Set true to trigger a rebake; cleared after GRID_BAKE

    /// Voxel-based config (optional, for scaling)
    float voxelSizeWorld_{1.0f};       ///< World units per 1 voxel
    int voxelsPerNavCell_{3};          ///< Nav cells span this many voxels
    float defaultAgentClearanceVoxels_{7.0f};  ///< Default player radius in voxels (6–8 typical)

    C_NavWorld()
        : origin_{0.0f}
        , cellSizeWorld_{1.0f}
        , chunkSize_{16, 16, 1} {}

    explicit C_NavWorld(float cellSizeWorld)
        : origin_{0.0f}
        , cellSizeWorld_{cellSizeWorld}
        , chunkSize_{16, 16, 1} {}

    /// Create from voxel-based config. cellSizeWorld = voxelSizeWorld * voxelsPerNavCell.
    static C_NavWorld fromVoxelConfig(
        float voxelSizeWorld,
        int voxelsPerNavCell,
        float defaultAgentClearanceVoxels = 7.0f
    ) {
        C_NavWorld w;
        w.voxelSizeWorld_ = voxelSizeWorld;
        w.voxelsPerNavCell_ = voxelsPerNavCell;
        w.defaultAgentClearanceVoxels_ = defaultAgentClearanceVoxels;
        w.cellSizeWorld_ = voxelSizeWorld * static_cast<float>(voxelsPerNavCell);
        w.chunkSize_ = IRMath::ivec3(16, 16, 1);
        return w;
    }

    float getDefaultAgentClearanceWorld() const {
        return voxelSizeWorld_ * defaultAgentClearanceVoxels_;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_NAV_WORLD_H */
