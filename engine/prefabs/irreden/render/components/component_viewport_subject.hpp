#ifndef COMPONENT_VIEWPORT_SUBJECT_H
#define COMPONENT_VIEWPORT_SUBJECT_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Tags an entity whose `C_VoxelSetNew` a secondary viewport draws, in
// addition to wherever the set already renders. `viewport_` is the id
// `IRPrefab::Viewport::create` returned.
struct C_ViewportSubject {
    IREntity::EntityId viewport_ = IREntity::kNullEntity;

    C_ViewportSubject() = default;

    explicit C_ViewportSubject(IREntity::EntityId viewport)
        : viewport_{viewport} {}
};

} // namespace IRComponents

#endif /* COMPONENT_VIEWPORT_SUBJECT_H */
