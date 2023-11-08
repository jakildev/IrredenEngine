/*
 * Project: Irreden Engine
 * File: ir_ecs.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Should this be the only thing implementers need to include?
// YES! I think so. Therefore, this file should only be an API
// and all files in the module don't need to include it, they will
// use internal files.

// IDEA: Should systems be a dependency on entities as far as ECS.
// Systems would link to Commands and Entities.


#ifndef IR_ECS_H
#define IR_ECS_H

#include <string>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ecs/ir_ecs_types.hpp>
#include <irreden/ecs/prefabs.hpp>
#include <irreden/ecs/entity_handle.hpp>

namespace IRECS {

    template <
        PrefabTypes type,
        typename... Args
    >
    EntityId createPrefab(Args&&... args) {
        return Prefab<type>::create(
            args...
        );
    }




    // EntityHandle createEntity();

}

#endif /* IR_ECS_H */
