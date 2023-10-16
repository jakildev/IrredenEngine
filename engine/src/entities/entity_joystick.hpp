/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_joystick.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_JOYSTICK_H
#define ENTITY_JOYSTICK_H

#include "..\ecs\ir_ecs.hpp"
#include "..\ecs\prefabs.hpp"
#include "..\math\ir_math.hpp"

#include "..\components\component_tags_all.hpp"
#include "..\components\component_glfw_joystick.hpp"
#include "..\components\component_name.hpp"
#include "..\components\component_glfw_gamepad_state.hpp"

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kGLFWJoystick> {
        // static Archetype archetype_;
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
