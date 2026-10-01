#ifndef COMPONENT_CANVAS_PART_H
#define COMPONENT_CANVAS_PART_H

#include <irreden/entity/ir_entity_types.hpp>

namespace IRComponents {

// Marks an entity's `C_VoxelSetNew` as a part of another entity's detached
// re-voxelize canvas. The part keeps its own transform: while its set is
// resident in the host's pool it is drawn there, posed by its own
// `C_WorldTransform` relative to the host, so several parts turn
// independently inside one canvas. The membership outlives residency — a part
// whose host has released its canvas falls back to GRID and is re-hosted when
// the host's canvas returns.
struct C_CanvasPart {
    // The entity owning the `C_EntityCanvas` this part is drawn into.
    IREntity::EntityId host_ = IREntity::kNullEntity;
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_PART_H */
