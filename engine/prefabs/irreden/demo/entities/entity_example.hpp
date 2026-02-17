#ifndef ENTITY_EXAMPLE_H
#define ENTITY_EXAMPLE_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/demo/components/component_example.hpp>

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

template <> struct Prefab<PrefabTypes::kExample> {
    static EntityHandle create() {
        EntityHandle entity{};
        entity.set(C_Example{});
        return entity;
    }
};
} // namespace IRECS

#endif /* ENTITY_EXAMPLE_H */
