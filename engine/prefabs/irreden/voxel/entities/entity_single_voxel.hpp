#ifndef ENTITY_SINGLE_VOXEL_H
#define ENTITY_SINGLE_VOXEL_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

template <> struct Prefab<PrefabTypes::kSingleVoxel> {
    static EntityId create(vec3 position, Color color = IRColors::kGreen) {
        return IRECS::createEntity(C_LocalTransform{position}, C_Voxel{color});
    }
};

} // namespace IRECS

#endif /* ENTITY_SINGLE_VOXEL_H */
