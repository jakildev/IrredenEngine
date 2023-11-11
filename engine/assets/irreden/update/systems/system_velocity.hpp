/*
 * Project: Irreden Engine
 * File: system_velocity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_VELOCITY_H
#define SYSTEM_VELOCITY_H

#include <irreden/system/system_base.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRInput;

namespace IRECS {

    // EntityCommands
    void stopVelocity(EntityHandle entity) {
        IRProfile::engLogDebug("Stopping velocity for entity {}.", entity.getId());
        entity.remove<C_Velocity3D>();
    }

    template<>
    class System<VELOCITY_3D> : public SystemBase<
        VELOCITY_3D,
        C_Position3D,
        C_Velocity3D
    >   {
    public:
        System() {
            IRProfile::engLogInfo("Created system VELOCITY_3D");
        }
        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Position3D>& positions,
            std::vector<C_Velocity3D>& velocities
        )
        {
            IR_PROFILE_BLOCK("VELOCITY_3D!!!", IR_PROFILER_COLOR_RENDER);
            for(int i=0; i < entities.size(); i++) {
                positions[i].pos_ += (
                    velocities[i].velocity_
                );
            }
        }
    private:
        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };


} // namespace IRECS

// namespace IRECS {

    void systemUpdateVelocity(
        C_Position3D& position,
        const C_Velocity3D& velocity
    )
    {
        position.pos_ += velocity.velocity_;
    }

// } // namespace IRECS


#endif /* SYSTEM_VELOCITY_H */
