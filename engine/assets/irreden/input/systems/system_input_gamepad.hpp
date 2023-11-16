    /*
 * Project: Irreden Engine
 * File: system_input_gamepad.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_GAMEPAD_H
#define SYSTEM_INPUT_GAMEPAD_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/input/components/component_glfw_joystick.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>
#include <irreden/input/entities/entity_joystick.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class System<INPUT_GAMEPAD> : public SystemBase<
        INPUT_GAMEPAD,
        C_GLFWJoystick,
        C_GLFWGamepadState
    >   {
    public:
        System(IRInput::IRGLFWWindow& window)
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

        // bool checkButton(
        //     InputTypes buttonType,
        //     GamepadButtons button,
        //     int irGamepadId = 0
        // )
        // {
        //     const auto& gamepadState = m_gamepadEntities[irGamepadId]
        //         .get<C_GLFWGamepadState>();
        //     if(buttonType == InputTypes::kGamepadButtonPressed) {
        //         return gamepadState.checkButtonPressed(button);
        //     }
        //     if(buttonType == InputTypes::kGamepadButtonReleased) {
        //         return gamepadState.checkButtonReleased(button);
        //     }
        //     if(buttonType == InputTypes::kGamepadButtonDown) {
        //         return gamepadState.checkButtonDown(button);
        //     }
        //     return false;
        // }

        float getAxisValue(
            GamepadAxes axis,
            int irGamepadId = 0
        )
        {
            return IRECS::getComponent<C_GLFWGamepadState>(
                m_gamepadEntities[irGamepadId]
            ).getAxisValue(axis);
        }

    private:
        IRInput::IRGLFWWindow& m_window;
        std::vector<EntityId> m_gamepadEntities;

        virtual void beginExecute() override {

        }

        virtual void endExecute() override {

        }

        void initJoystickEntities() {
            for(int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
            {
                if(m_window.joystickPresent(i)) {
                    IRProfile::engLogInfo("Creating joystick entity for joystick {}", i);

                    m_gamepadEntities.emplace_back(
                        IRECS::createEntity<kGLFWJoystick>(
                             i,
                            m_window.getJoystickName(i),
                            m_window.joystickIsGamepad(i)
                        )
                    );
                }
            }
        }
    };

} // namespace IRECS

#endif /* SYSTEM_INPUT_GAMEPAD_H */
