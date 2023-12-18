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

#include <optional>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    // Leftg off here why no work

    template<>
    struct System<GLOBAL_POSITION_3D> {
        static constexpr SystemId create() {
            // Example of heirarchical system that has optional parent
            return createSystem<
                C_Position3D,
                C_PositionGlobal3D
            >(
                "UpdatePositionsGlobal",
                [](
                    C_Position3D& position,
                    C_PositionGlobal3D& positionGlobal,
                    const std::optional<C_PositionGlobal3D*> parentPositionGlobal
                )
                {
                    static C_PositionGlobal3D defaultParentPosition = {0, 0, 0};
                    positionGlobal.pos_ = position.pos_ + parentPositionGlobal.value_or(
                        &defaultParentPosition
                    )->pos_;
                },
                nullptr,
                nullptr,
                RelationParams<C_PositionGlobal3D>{
                    .relation_ = Relation::CHILD_OF
                }
            );
        }
    };


} // namespace IRECS

#endif /* SYSTEM_UPDATE_POSITIONS_GLOBAL_H */
