#ifndef ENTITY_SINGLE_VOXEL_H
#define ENTITY_SINGLE_VOXEL_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

template <> struct Prefab<PrefabTypes::kSingleVoxel> {
    static EntityId create(vec3 position, Color color = IRColors::kGreen) {
        return IRECS::createEntity(C_Position3D{position}, C_PositionGlobal3D{position},
                                   C_PositionOffset3D{vec3(0, 0, 0)}, C_Voxel{color});
    }
};

} // namespace IRECS

#endif /* ENTITY_SINGLE_VOXEL_H */