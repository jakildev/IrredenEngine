#ifndef COMPONENT_CONTACT_EVENT_H
#define COMPONENT_CONTACT_EVENT_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>

using IRMath::vec3;

namespace IRComponents {

struct C_ContactEvent {
    bool entered_;
    bool stayed_;
    bool exited_;
    bool touching_;
    bool wasTouching_;
    IREntity::EntityId otherEntity_;
    vec3 normal_;

    C_ContactEvent()
        : entered_{false}
        , stayed_{false}
        , exited_{false}
        , touching_{false}
        , wasTouching_{false}
        , otherEntity_{IREntity::kNullEntity}
        , normal_{vec3(0.0f)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CONTACT_EVENT_H */
