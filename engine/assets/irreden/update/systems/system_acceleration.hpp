/*
 * Project: Irreden Engine
 * File: system_acceleration.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_ACCELERATION_H
#define SYSTEM_ACCELERATION_H

#include <irreden/ir_ecs.hpp>

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>

using namespace IRComponents;

namespace IRECS {

    template<>
    class System<ACCELERATION_3D> : public SystemBase<
        ACCELERATION_3D,
        C_Velocity3D,
        C_Acceleration3D
    >   {
    public:
        System()
        {
            IRProfile::engLogInfo("Created system ACCELERATION_3D");
        }
        virtual ~System() = default;

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            std::vector<C_Velocity3D>& velocities,
            std::vector<C_Acceleration3D>& accelerations
        )
        {
            for(int i=0; i < entities.size(); i++) {
                velocities[i].velocity_ += accelerations[i].acceleration_;
            }
        }
    private:
        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };

} // namespace IRECS

#endif /* SYSTEM_ACCELERATION_H */