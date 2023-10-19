/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_gravity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_GRAVITY_H
#define SYSTEM_GRAVITY_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_velocity_3d.hpp"
#include "..\components\component_gravity_3d.hpp"

using namespace IRComponents;

namespace IRECS {

    template<>
    class IRSystem<GRAVITY_3D> : public IRSystemBase<
        GRAVITY_3D,
        C_Velocity3D
    >   {
    public:
        IRSystem(
            C_Gravity3D gravity = C_Gravity3D{}
        )
        :   m_gravity{gravity}
        {
            addTag<C_HasGravity>();
            // TODO: Move commands out of here
            registerCommand<kKeyMouseButtonPressed>(
                IRKeyMouseButtons::kKeyButtonG,
                [this]() {
                    this->invertGravity();
                }
            );
            ENG_LOG_INFO("Created system GRAVITY_3D");
        }
        virtual ~IRSystem() = default;

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            std::vector<C_Velocity3D>& velocities
        )
        {
            vec3 gravityVector = m_gravity.getVector();
            for(int i=0; i < entities.size(); i++) {
                velocities[i].velocity_ += gravityVector;
            }
        }

    private:
        C_Gravity3D m_gravity; // TODO components at system level in ECS graph
        virtual void beginExecute() override {}
        virtual void endExecute() override {}

        void invertGravity() {
            m_gravity.setDirection(m_gravity.direction_.direction_ * -1.0f);
        }
    };

} // namespace IRECS

#endif /* SYSTEM_GRAVITY_H */