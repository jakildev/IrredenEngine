/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\ir_ecs.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Should this be the only thing implementers need to include?

#ifndef IR_ECS_H
#define IR_ECS_H

#include <cstdint>
#include <set>
#include <string>

// IDEA: C_Position2D => C_Position3D // depends on???
// IDEA: entity.set(C_Position2D, [](C_Position3D position3D)
    //                               { newComponents.pos_ = })
    // Essentially lambda function for a component that defines how it interacts with
    // other components each frame

namespace IRECS {
    using EntityId = std::uint64_t;
    using ComponentId = EntityId;
    using Archetype = std::set<ComponentId>;

    constexpr EntityId IR_MAX_ENTITIES =                        0x0000000001FFFFFF;
    constexpr EntityId IR_RESERVED_ENTITIES =                   0x00000000000000FF;
    constexpr EntityId IR_ENTITY_ID_BITS =                      0x00000000FFFFFFFF;
    constexpr EntityId IR_PURE_ENTITY_BIT =                     0x0000000100000000;
    constexpr EntityId IR_ENTITY_FLAG_MARKED_FOR_DELETION =     0x8000000000000000;
    constexpr EntityId kNullEntityId = 0;

    // TODO: Move this
    std::string makeComponentString(Archetype type);

    enum IRRelationType {
        CHILD_OF,
        PARENT_TO,
        SIBLING_OF    };

    enum class IREvents {
        START,
        END
    };

}

#endif /* IR_ECS_H */
