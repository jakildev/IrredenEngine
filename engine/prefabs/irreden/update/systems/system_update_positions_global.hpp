/*
 * Project: Irreden Engine
 * File: system_update_positions_global.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_UPDATE_POSITIONS_GLOBAL_H
#define SYSTEM_UPDATE_POSITIONS_GLOBAL_H

#include <irreden/ir_ecs.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    struct System<GLOBAL_POSITION_3D> {
        static constexpr SystemId create() {
            return createSystem<C_Position3D, C_PositionGlobal3D>(
                "UpdatePositionsGlobal",
                [](
                    const Archetype& archetype,
                    std::vector<EntityId>& entities,
                    std::vector<C_Position3D>& positions,
                    std::vector<C_PositionGlobal3D>& positionsGlobal
                )
                {
                    C_PositionGlobal3D parentPositionGlobal{0, 0, 0};
                    EntityId parent = getParentEntityFromArchetype(archetype);
                    if(parent != kNullEntity) {
                        parentPositionGlobal = IRECS::getComponent<C_PositionGlobal3D>(parent);
                    }
                    for(int i=0; i < entities.size(); i++) {
                        positionsGlobal[i].pos_ = positions[i].pos_ + parentPositionGlobal.pos_;
                    }
                },
                nullptr,
                nullptr,
                {
                    .relation_ = Relation::CHILD_OF
                }
            );
        }
    };


} // namespace IRECS

#endif /* SYSTEM_UPDATE_POSITIONS_GLOBAL_H */
