#ifndef SYSTEM_COLLISION_EVENT_CLEAR_H
#define SYSTEM_COLLISION_EVENT_CLEAR_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_contact_event.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<COLLISION_EVENT_CLEAR> {
    static SystemId create() {
        return createSystem<C_ContactEvent>("CollisionEventClear", [](C_ContactEvent &contactEvent) {
            contactEvent.wasTouching_ = contactEvent.touching_;
            contactEvent.entered_ = false;
            contactEvent.stayed_ = false;
            contactEvent.exited_ = contactEvent.wasTouching_;
            contactEvent.touching_ = false;
            contactEvent.otherEntity_ = IREntity::kNullEntity;
            contactEvent.normal_ = vec3(0.0f);
        });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COLLISION_EVENT_CLEAR_H */
