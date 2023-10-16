/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_single_voxel.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_SINGLE_VOXEL_H
#define ENTITY_SINGLE_VOXEL_H

#include "..\ecs\entity_handle.hpp"
#include "..\ecs\prefabs.hpp"
#include "..\math\ir_math.hpp"
#include "..\components\component_tags_all.hpp"

#include "..\components\component_position_3d.hpp"
#include "..\components\component_position_offset_3d.hpp"
#include "..\components\component_position_global_3d.hpp"
#include "..\components\component_voxel.hpp"

using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kSingleVoxel> {
        static EntityHandle create(
            vec3 position,
            Color color = IRConstants::kColorGreen
        )
        {
            EntityHandle entity{};
            entity.set(C_Position3D{position});
            entity.set(C_PositionGlobal3D{position});
            entity.set(C_PositionOffset3D{vec3(0, 0, 0)});
            entity.set(C_Voxel{color});
            return entity;
        }
    };

}

#endif /* ENTITY_SINGLE_VOXEL_H */
