/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_example.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_EXAMPLE_H
#define ENTITY_EXAMPLE_H

#include "../entity/prefabs.hpp"
#include "../entity/entity_handle.hpp"
#include "../math/ir_math.hpp"
#include "../game_components/component_tags_all.hpp"

#include "../game_components/component_example.hpp"

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    template<>
    struct Prefab<PrefabTypes::kExample> {
        static EntityHandle create() {
            EntityHandle entity{};
            entity.set(C_Example{});
            // REMOVE BEFORE PUBLISH
            entity.set(C_IsNotPure{}); // This is what you have to do for now
            return entity;
        }
    };
}

#endif /* ENTITY_EXAMPLE_H */
