/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_input_gamepad.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_GAMEPAD_H
#define SYSTEM_INPUT_GAMEPAD_H

#include <irreden/ecs/ir_system_base.hpp>

#include <irreden/input/ir_glfw_window.hpp>
#include "..\entities\entity_joystick.hpp"

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class IRSystem<INPUT_GAMEPAD> : public IRSystemBase<
        INPUT_GAMEPAD,
        C_GLFWJoystick,
        C_GLFWGamepadState
    >   {
    public:
        IRSystem(IRGLFWWindow& window)
        :   m_window{window}
        {
            IRProfile::engLogInfo("Creating system INPUT_GAMEPAD");
            initJoystickEntities();
        }

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            std::vector<C_GLFWJoystick>& joysticks,
            std::vector<C_GLFWGamepadState>& gamepadState

        )
        {
            for(int i=0; i < entities.size(); i++) {
                gamepadState[i].updateState(
                    m_window.getGamepadState(joysticks[i].joystickId_)
                );
            }
        }

        bool checkButton(
            IRInputTypes buttonType,
            IRGamepadButtons button,
            int irGamepadId = 0
        )
        {
            const auto& gamepadState = m_gamepadEntities[irGamepadId]
                .get<C_GLFWGamepadState>();
            if(buttonType == IRInputTypes::kGamepadButtonPressed) {
                return gamepadState.checkButtonPressed(button);
            }
            if(buttonType == IRInputTypes::kGamepadButtonReleased) {
                return gamepadState.checkButtonReleased(button);
            }
            if(buttonType == IRInputTypes::kGamepadButtonDown) {
                return gamepadState.checkButtonDown(button);
            }
            return false;
        }

        float getAxisValue(
            IRGamepadAxes axis,
            int irGamepadId = 0
        )
        {
            const auto& gamepadState = m_gamepadEntities[irGamepadId]
                .get<C_GLFWGamepadState>();
            return gamepadState.getAxisValue(axis);
        }

    private:
        IRGLFWWindow& m_window;
        std::vector<EntityHandle> m_gamepadEntities;

        virtual void beginExecute() override {

        }

        virtual void endExecute() override {

        }

        void initJoystickEntities() {
            for(int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
            {
                if(m_window.joystickPresent(i)) {
                    IRProfile::engLogInfo("Creating joystick entity for joystick {}", i);
                    EntityHandle newJoystick = Prefab<PrefabTypes::kGLFWJoystick>::create(
                        i,
                        m_window.getJoystickName(i),
                        m_window.joystickIsGamepad(i)
                    );
                    m_gamepadEntities.push_back(newJoystick);
                }
            }
        }
    };

} // namespace IRSystem

#endif /* SYSTEM_INPUT_GAMEPAD_H */