/*
 * Project: Irreden Engine
 * File: input_manager.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/input/input_manager.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>
#include <irreden/input/entities/entity_joystick.hpp>

namespace IRInput {
    InputManager::InputManager()
    :   m_scrollEntitiesThisFrame{}
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
            getWindow().getKeysPressedToProcess(),
            ButtonStatuses::PRESSED
        );
        processKeyMouseButtons(
            getWindow().getKeysReleasedToProcess(),
            ButtonStatuses::RELEASED
        );
        processKeyMouseButtons(
            getWindow().getMouseButtonsPressedToProcess(),
            ButtonStatuses::PRESSED
        );
        processKeyMouseButtons(
            getWindow().getMouseButtonsReleasedToProcess(),
            ButtonStatuses::RELEASED
        );
        processScrolls(getWindow().getScrollsToProcess());
        IRInput::getCursorPosition(
            m_mousePositionUpdate
        );
    }

    void InputManager::tickRender() {
        IRInput::getCursorPosition(
            m_mousePositionRender
        );
    }

    ButtonStatuses InputManager::getButtonStatus(KeyMouseButtons button) const {
        return IREntity::getComponent<C_KeyStatus>(
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

    vec2 InputManager::getMousePositionUpdate() const {
        return vec2(m_mousePositionUpdate);
    }
    vec2 InputManager::getMousePositionRender() const {
        return vec2(m_mousePositionRender);
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
        return IREntity::getComponent<C_GLFWGamepadState>(
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

            IRE_LOG_DEBUG(
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
                IREntity::createEntity<kMouseScroll>(scroll.first, scroll.second);
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
            if(getWindow().joystickPresent(i)) {
                IRE_LOG_INFO("Creating joystick entity for joystick {}", i);

                m_gamepadEntities.emplace_back(
                    IREntity::createEntity<kGLFWJoystick>(
                         i,
                        getWindow().getJoystickName(i),
                        getWindow().joystickIsGamepad(i)
                    )
                );
            }
        }
    }

} // namespace IRInput