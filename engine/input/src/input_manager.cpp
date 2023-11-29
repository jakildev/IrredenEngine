/*
 * Project: Irreden Engine
 * File: input_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>

#include <irreden/input/input_manager.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>
#include <irreden/input/entities/entity_joystick.hpp>

namespace IRInput {
    InputManager::InputManager(IRGLFWWindow& window)
    :   m_window{window}
    ,   m_scrollEntitiesThisFrame{}
    ,   m_buttonPressesThisFrame{}
    ,   m_buttonReleasesThisFrame{}
    ,   m_mousePositionUpdate{}
    ,   m_mousePositionRender{}
    {
        m_buttonPressesThisFrame.resize(
            static_cast<int>(kNumKeyMouseButtons)
        );
        m_buttonReleasesThisFrame.resize(
            static_cast<int>(kNumKeyMouseButtons)
        );

        initKeyMouseButtonEntities();
        initJoystickEntities();

        g_inputManager = this;
        IRE_LOG_INFO("Created InputManager");
    }

    InputManager::~InputManager() {
        IRE_LOG_INFO("Destroyed InputManager");
    }

    void InputManager::tick() {

        m_scrollEntitiesThisFrame.clear();
        std::fill(
            m_buttonPressesThisFrame.begin(),
            m_buttonPressesThisFrame.end(),
            0
        );
        std::fill(
            m_buttonReleasesThisFrame.begin(),
            m_buttonReleasesThisFrame.end(),
            0
        );

        processKeyMouseButtons(
            m_window.getKeysPressedToProcess(),
            ButtonStatuses::PRESSED
        );
        processKeyMouseButtons(
            m_window.getKeysReleasedToProcess(),
            ButtonStatuses::RELEASED
        );
        processKeyMouseButtons(
            m_window.getMouseButtonsPressedToProcess(),
            ButtonStatuses::PRESSED
        );
        processKeyMouseButtons(
            m_window.getMouseButtonsReleasedToProcess(),
            ButtonStatuses::RELEASED
        );
        processScrolls(m_window.getScrollsToProcess());
        IRInput::getCursorPosition(
            m_mousePositionUpdate.pos_
        );
    }

    void InputManager::tickRender() {
        IRInput::getCursorPosition(
            m_mousePositionRender.pos_
        );
    }

    ButtonStatuses InputManager::getButtonStatus(KeyMouseButtons button) const {
        return IRECS::getComponent<C_KeyStatus>(
            m_keyMouseButtonEntities.at(button)
        ).status_;
    }

    bool InputManager::checkButtonPressed(KeyMouseButtons button) const {
        return
            getButtonStatus(button) == ButtonStatuses::PRESSED ||
            getButtonStatus(button) == ButtonStatuses::PRESSED_AND_RELEASED;
    }

    bool InputManager::checkButtonDown(KeyMouseButtons button) const {
        return
            getButtonStatus(button) == ButtonStatuses::PRESSED ||
            getButtonStatus(button) == ButtonStatuses::HELD;
    }

    bool InputManager::checkButtonReleased(KeyMouseButtons button) const {
        return
            getButtonStatus(button) == ButtonStatuses::RELEASED ||
            getButtonStatus(button) == ButtonStatuses::PRESSED_AND_RELEASED;
    }

    bool InputManager::checkButton(KeyMouseButtons button, ButtonStatuses status) const {
        if(status == ButtonStatuses::PRESSED) {
            return checkButtonPressed(button);
        }
        if(status == ButtonStatuses::RELEASED) {
            return checkButtonReleased(button);
        }
        if(status == ButtonStatuses::HELD) {
            return checkButtonDown(button);
        }
        IR_ASSERT(false, "Invalid button status to check");
        return false;
    }

    C_MousePosition InputManager::getMousePositionUpdate() const {
        return m_mousePositionUpdate;
    }

    C_MousePosition InputManager::getMousePositionRender() const {
        return m_mousePositionRender;
    }

    int InputManager::getButtonPressesThisFrame(KeyMouseButtons button) const {
        return m_buttonPressesThisFrame.at(
            static_cast<int>(button)
        );
    }

    int InputManager::getButtonReleasesThisFrame(KeyMouseButtons button) const {
        return m_buttonReleasesThisFrame.at(
            static_cast<int>(button)
        );
    }

    float InputManager::getAxisValue(
        GamepadAxes axis,
        int irGamepadId
    ) const
    {
        return IRECS::getComponent<C_GLFWGamepadState>(
            m_gamepadEntities.at(irGamepadId)
        ).getAxisValue(axis);
    }

    void InputManager::processKeyMouseButtons(
        std::queue<int>& queueOfButtons,
        ButtonStatuses status
    )
    {
        while(!queueOfButtons.empty()) {
            int button = queueOfButtons.front();
            KeyMouseButtons irButton = kMapGLFWtoIRKeyMouseButtons.at(button);
            if(status == ButtonStatuses::PRESSED) {
                ++m_buttonPressesThisFrame[irButton];
            }
            if(status == ButtonStatuses::RELEASED) {
                ++m_buttonReleasesThisFrame[irButton];

            }
            queueOfButtons.pop();

            IRE_LOG_INFO(
                "Processed button={}, status={}",
                button,
                static_cast<int>(status)
            );
        }
    }

    void InputManager::processScrolls(
        std::queue<std::pair<double, double>>& queueOfScrolls
    )
    {
        while(!queueOfScrolls.empty()) {
            std::pair<double, double> scroll = queueOfScrolls.front();
            EntityId entityScroll =
                IRECS::createEntity<kMouseScroll>(scroll.first, scroll.second);
            m_scrollEntitiesThisFrame.push_back(entityScroll);
            queueOfScrolls.pop();

            IRProfile::engLogDebug(
                "Processed scroll xoffset={}, yoffset={}",
                scroll.first,
                scroll.second
            );
        }
    }

    void InputManager::initKeyMouseButtonEntities() {
        for(int i = 0; i < KeyMouseButtons::kNumKeyMouseButtons; ++i) {
            EntityId entityNewButton = Prefab<kKeyMouseButton>::create(
                static_cast<KeyMouseButtons>(i)
            );
            m_keyMouseButtonEntities.insert({
                static_cast<KeyMouseButtons>(i),
                entityNewButton
            });
        }
    }

    void InputManager::initJoystickEntities() {
        for(int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++)
        {
            if(m_window.joystickPresent(i)) {
                IRE_LOG_INFO("Creating joystick entity for joystick {}", i);

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

} // namespace IRInput