/*
 * Project: Irreden Engine
 * File: entity_joystick.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_JOYSTICK_H
#define ENTITY_JOYSTICK_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/input/components/component_glfw_joystick.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>

using namespace IRComponents;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kGLFWJoystick> {
        static EntityId create(
            int joystickId,
            std::string name,
            bool isGamepad
        )
        {
            EntityId entity = IRECS::createEntity(
                C_GLFWJoystick{joystickId}
            );
            if(isGamepad)
                IRECS::setComponent(entity, C_GLFWGamepadState{});
            return entity;
        }

        // static getArchetypeFromCreate()
    };
} // namespace IRECS

#endif /* ENTITY_JOYSTICK_H */
