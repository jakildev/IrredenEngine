#include <irreden/ir_input.hpp>

#include <irreden/input/input_manager.hpp>

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

        for(int i = 0; i < KeyMouseButtons::kNumKeyMouseButtons; ++i) {
            EntityId entityNewButton = Prefab<kKeyMouseButton>::create(
                static_cast<KeyMouseButtons>(i)
            );
            m_keyMouseButtonEntities.insert({
                static_cast<KeyMouseButtons>(i),
                entityNewButton
            });
        }
        g_inputManager = this;
        IRProfile::engLogInfo("Created InputManager");
    }

    InputManager::~InputManager() {
        IRProfile::engLogInfo("Destroyed InputManager");
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
        m_window.getUpdateCursorPos(
            m_mousePositionUpdate.pos_.x,
            m_mousePositionUpdate.pos_.y
        );
    }

     void InputManager::tickRender() {
        m_window.getUpdateCursorPos(
            m_mousePositionRender.pos_.x,
            m_mousePositionRender.pos_.y
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

} // namespace IRInput