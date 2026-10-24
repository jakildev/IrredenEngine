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
    // World translation at the previous COLLISION_NOTE_PLATFORM tick: where
    // this collider's next sweep starts. Unset until that system has seen the
    // collider once, so a collider's first tick is never swept.
    vec3 previousTranslation_;
    bool hasPreviousTranslation_;

    // A collider built inside another never enters it: born touching, its
    // first overlap is a stay, so nothing keyed on `entered_` (a landing, a
    // contact note, a spring) fires at the build.
    explicit C_ContactEvent(bool bornTouching)
        : entered_{false}
        , stayed_{false}
        , exited_{false}
        , touching_{bornTouching}
        , wasTouching_{bornTouching}
        , otherEntity_{IREntity::kNullEntity}
        , normal_{vec3(0.0f)}
        , previousTranslation_{vec3(0.0f)}
        , hasPreviousTranslation_{false} {}

    C_ContactEvent()
        : entered_{false}
        , stayed_{false}
        , exited_{false}
        , touching_{false}
        , wasTouching_{false}
        , otherEntity_{IREntity::kNullEntity}
        , normal_{vec3(0.0f)}
        , previousTranslation_{vec3(0.0f)}
        , hasPreviousTranslation_{false} {}
};

} // namespace IRComponents

#endif /* COMPONENT_CONTACT_EVENT_H */
