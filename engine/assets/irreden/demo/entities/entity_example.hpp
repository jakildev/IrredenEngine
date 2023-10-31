/*
 * Project: Irreden Engine
 * File: entity_example.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_EXAMPLE_H
#define ENTITY_EXAMPLE_H

#include <irreden/ecs/prefabs.hpp>
#include <irreden/ecs/entity_handle.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_tags_all.hpp>

#include <irreden/demo/components/component_example.hpp>

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
