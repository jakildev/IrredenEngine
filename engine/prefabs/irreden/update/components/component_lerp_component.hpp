#ifndef COMPONENT_LERP_COMPONENT_H
#define COMPONENT_LERP_COMPONENT_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>

// LEFT OFF HERE, all this needs work.
// do i need to also take in a min and max value,
// or can those be static in the function lambda?
// do I need to take in a specific easing function?
// prob

using namespace IRMath;

namespace IRComponents {

struct C_LerpEntity {
    EntityId boundEntity_;
    std::function<void(EntityId)> func_;

    template <typename Function>
    C_LerpComponent(EntityId entity, Function func)
        : func_(func) {}

    // Default
    C_LerpComponent()
        : {}

    void tick() {
        func_(boundEntity_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_LERP_COMPONENT_H */
