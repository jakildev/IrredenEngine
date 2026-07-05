#ifndef COMPONENT_LERP_COMPONENT_H
#define COMPONENT_LERP_COMPONENT_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/ir_constants.hpp>

#include <functional>

// LEFT OFF HERE, all this needs work.
// do i need to also take in a min and max value,
// or can those be static in the function lambda?
// do I need to take in a specific easing function?
// prob

using namespace IRMath;

namespace IRComponents {

struct C_LerpEntity {
    IREntity::EntityId boundEntity_;
    std::function<void(IREntity::EntityId)> func_;

    template <typename Function>
    C_LerpEntity(IREntity::EntityId entity, Function func)
        : boundEntity_(entity)
        , func_(func) {}

    C_LerpEntity() = default;

    void tick() {
        func_(boundEntity_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_LERP_COMPONENT_H */
