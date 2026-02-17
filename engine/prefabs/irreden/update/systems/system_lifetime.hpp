#ifndef SYSTEM_LIFETIME_H
#define SYSTEM_LIFETIME_H

#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<LIFETIME> {
    static SystemId create() {
        return createSystem<C_Lifetime>("Lifetime", [](EntityId entity, C_Lifetime &lifetime) {
            lifetime.life_--;
            if (lifetime.life_ <= 0) {
                IREntity::destroyEntity(entity);
            }
        });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LIFETIME_H */
