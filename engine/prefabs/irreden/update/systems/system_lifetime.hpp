/*
 * Project: Irreden Engine
 * File: system_lifetime.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_LIFETIME_H
#define SYSTEM_LIFETIME_H

#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    struct System<LIFETIME> {
        static SystemId create() {
            return createSystem<C_Lifetime>(
                "Lifetime",
                [](
                    EntityId entity,
                    C_Lifetime& lifetime
                )
                {
                    lifetime.life_--;
                    if(lifetime.life_ <= 0) {
                        IRECS::destroyEntity(entity);
                    }
                }
            );
        }
    };

} // namespace IRECS

#endif /* SYSTEM_LIFETIME_H */
