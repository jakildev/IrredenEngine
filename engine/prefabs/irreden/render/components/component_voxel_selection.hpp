#ifndef COMPONENT_VOXEL_SELECTION_H
#define COMPONENT_VOXEL_SELECTION_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

namespace IRComponents {

// Holds the editor viewport's current voxel selection. Designed as a
// singleton (one instance per world, owned by the highlight entity that
// also carries C_VoxelSelectionHighlight + C_ShapeDescriptor); the
// picking system mutates this row when the user left-clicks. Phase 1+
// multi-select tools will replace the single ivec3 with a span or set.
struct C_VoxelSelection {
    bool hasSelection_ = false;
    IRMath::ivec3 voxelPos_ = IRMath::ivec3(0);
    IRMath::vec3 worldHitPos_ = IRMath::vec3(0.0f);
    IREntity::EntityId hitEntity_ = IREntity::kNullEntity;
};

// Tag identifying the visual highlight entity. The picking system both
// reads C_VoxelSelection state from this entity and writes the entity's
// C_Position3D / C_ShapeDescriptor visibility flag.
struct C_VoxelSelectionHighlight {};

} // namespace IRComponents

#endif /* COMPONENT_VOXEL_SELECTION_H */
