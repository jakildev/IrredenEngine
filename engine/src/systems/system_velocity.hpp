/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_velocity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_VELOCITY_H
#define SYSTEM_VELOCITY_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_position_3d.hpp"
#include "..\components\component_velocity_3d.hpp"

using namespace IRComponents;
using namespace IRRendering;
using namespace IRInput;

namespace IRECS {

    // EntityCommands
    void stopVelocity(EntityHandle entity) {
        ENG_LOG_DEBUG("Stopping velocity for entity {}.", entity.getId());
        entity.remove<C_Velocity3D>();
    }

    template<>
    class IRSystem<VELOCITY_3D> : public IRSystemBase<
        VELOCITY_3D,
        C_Position3D,
        C_Velocity3D
    >   {
    public:
        IRSystem() {
            // registerCommand<kGamepadButtonPressed>(
            //     IRGamepadButtons::kGamepadButtonA,
            //     [this]() {
            //         this->invertVelocitiesAll();
            //     }
            // );

            // TODO: Move commands out
            registerCommand<STOP_VELOCITY, kKeyMouseButtonPressed>(
                IRKeyMouseButtons::kKeyButtonS,
                &stopVelocity
            );
            ENG_LOG_INFO("Created system VELOCITY_3D");
        }
        virtual ~IRSystem() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities,
            std::vector<C_Position3D>& positions,
            std::vector<C_Velocity3D>& velocities
        )
        {
            for(int i=0; i < entities.size(); i++) {
                positions[i].pos_ += (
                    velocities[i].velocity_ * m_velocityModifier
                );
            }
        }

        // SystemCommands
        void invertVelocitiesAll() {
            ENG_LOG_INFO("Inverting all velocities.");
            m_velocityModifier *= -1.0f;

        }

    private:
        vec3 m_velocityModifier = vec3(1.0f, 1.0f, 1.0f);

        virtual void beginExecute() override {}
        virtual void endExecute() override {}
    };


} // namespace IRECS


#endif /* SYSTEM_VELOCITY_H */
