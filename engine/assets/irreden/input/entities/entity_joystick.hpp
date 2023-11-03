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
        static EntityHandle create(
            int joystickId,
            std::string name,
            bool isGamepad
        )
        {
            EntityHandle entity{};
            entity.set(C_GLFWJoystick{joystickId});
            entity.set(C_Name{name});
            if(isGamepad) entity.set(C_GLFWGamepadState{});

            return entity;
        }

        // static getArchetypeFromCreate()
    };
} // namespace IRECS

#endif /* ENTITY_JOYSTICK_H */
