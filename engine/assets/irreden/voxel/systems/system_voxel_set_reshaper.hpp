/*
 * Project: Irreden Engine
 * File: system_voxel_set_reshaper.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Fun experimental system to change shape of all voxel sets

#ifndef SYSTEM_VOXEL_SET_RESHAPER_H
#define SYSTEM_VOXEL_SET_RESHAPER_H

#include <irreden/ir_ecs.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    void reshapeVoxelSet(EntityId entity, Shape3D shape) {
        IRECS::getComponent<C_VoxelSetNew>(entity).reshape(shape);
    }

    template<>
    class System<VOXEL_SET_RESHAPER> : public SystemBase<
        VOXEL_SET_RESHAPER
    >   {
    public:
        System() {
            // TOOD: Move commands
            // registerCommand<RESHAPE_SPHERE, kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonS,
            //     &reshapeVoxelSet,
            //     Shape3D::SPHERE
            // );
            // registerCommand<RESHAPE_RECTANGULAR_PRISM, kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonC,
            //     &reshapeVoxelSet,
            //     Shape3D::RECTANGULAR_PRISM
            // );

            IRProfile::engLogInfo("Created system VOXEL_SET_RESHAPER");
        }
        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities
        )
        {

        }
    private:
        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };
} // namespace IRECS

#endif /* SYSTEM_VOXEL_SET_RESHAPER_H */
