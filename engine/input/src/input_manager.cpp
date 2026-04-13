#include <irreden/ir_input.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>

#include <algorithm>

#include <irreden/input/input_manager.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>
#include <irreden/input/entities/entity_joystick.hpp>

namespace IRInput {

void EventInputState::resize(int numButtons) {
    buttonStates_.resize(numButtons, ButtonStatuses::NOT_HELD);
    pressAccumulator_.resize(numButtons, false);
    releaseAccumulator_.resize(numButtons, false);
    mousePosition_ = {0.0, 0.0};
}

InputManager::InputManager()
    : m_scrollEntitiesThisFrame{}
    , m_buttonPressesThisFrame{}
    , m_buttonReleasesThisFrame{} {
    m_buttonPressesThisFrame.resize(static_cast<int>(kNumKeyMouseButtons));
    m_buttonReleasesThisFrame.resize(static_cast<int>(kNumKeyMouseButtons));

    for (IRTime::Events event : kTrackedEvents) {
        m_eventStates[event].resize(static_cast<int>(kNumKeyMouseButtons));
    }

    initKeyMouseButtonEntities();
    initJoystickEntities();

    g_inputManager = this;
    IRE_LOG_INFO("Created InputManager");
}

InputManager::~InputManager() {
    if (g_inputManager == this) {
        g_inputManager = nullptr;
    }
    IRE_LOG_INFO("Destroyed InputManager");
}

void InputManager::tick() {
    m_scrollEntitiesThisFrame.clear();
    std::fill(m_buttonPressesThisFrame.begin(), m_buttonPressesThisFrame.end(), 0);
    std::fill(m_buttonReleasesThisFrame.begin(), m_buttonReleasesThisFrame.end(), 0);

    processKeyMouseButtons(
        IRWindow::getWindow().getKeysPressedToProcess(),
        ButtonStatuses::PRESSED
    );
    processKeyMouseButtons(
        IRWindow::getWindow().getKeysReleasedToProcess(),
        ButtonStatuses::RELEASED
    );
    processKeyMouseButtons(
        IRWindow::getWindow().getMouseButtonsPressedToProcess(),
        ButtonStatuses::PRESSED
    );
    processKeyMouseButtons(
        IRWindow::getWindow().getMouseButtonsReleasedToProcess(),
        ButtonStatuses::RELEASED
    );
    processScrolls(IRWindow::getWindow().getScrollsToProcess());
}

// TODO: Do we need pressed and resleased as its own state, or can we get away
// with handling that just by looking at pressed and relesaed individually.
void InputManager::advanceInputState(IRTime::Events event) {
    m_currentEvent = event;
    EventInputState &state = m_eventStates[event];
    const int numButtons = static_cast<int>(state.buttonStates_.size());

    for (int i = 0; i < numButtons; ++i) {
        ButtonStatuses current = state.buttonStates_[i];
        if (current == ButtonStatuses::PRESSED) {
            current = ButtonStatuses::HELD;
        } else if (current == ButtonStatuses::RELEASED ||
                   current == ButtonStatuses::PRESSED_AND_RELEASED) {
            current = ButtonStatuses::NOT_HELD;
        }

        const bool pressed = state.pressAccumulator_[i];
        const bool released = state.releaseAccumulator_[i];
        if (pressed && released) {
            current = ButtonStatuses::PRESSED_AND_RELEASED;
        } else if (pressed) {
            current = ButtonStatuses::PRESSED;
        } else if (released) {
            current = ButtonStatuses::RELEASED;
        }

        state.buttonStates_[i] = current;
    }

    std::fill(state.pressAccumulator_.begin(), state.pressAccumulator_.end(), false);
    std::fill(state.releaseAccumulator_.begin(), state.releaseAccumulator_.end(), false);
    IRWindow::getCursorPosition(state.mousePosition_);
}

ButtonStatuses InputManager::getButtonStatus(KeyMouseButtons button) const {
    return currentEventState().buttonStates_.at(static_cast<int>(button));
}

bool InputManager::checkButtonPressed(KeyMouseButtons button) const {
    ButtonStatuses status = getButtonStatus(button);
    return status == ButtonStatuses::PRESSED ||
           status == ButtonStatuses::PRESSED_AND_RELEASED;
}

bool InputManager::checkButtonDown(KeyMouseButtons button) const {
    ButtonStatuses status = getButtonStatus(button);
    return status == ButtonStatuses::PRESSED ||
           status == ButtonStatuses::HELD;
}

bool InputManager::checkButtonReleased(KeyMouseButtons button) const {
    ButtonStatuses status = getButtonStatus(button);
    return status == ButtonStatuses::RELEASED ||
           status == ButtonStatuses::PRESSED_AND_RELEASED;
}

bool InputManager::checkButton(KeyMouseButtons button, ButtonStatuses status) const {
    if (status == ButtonStatuses::PRESSED) {
        return checkButtonPressed(button);
    }
    if (status == ButtonStatuses::RELEASED) {
        return checkButtonReleased(button);
    }
    if (status == ButtonStatuses::HELD) {
        return checkButtonDown(button);
    }
    IR_ASSERT(false, "Invalid button status to check");
    return false;
}

vec2 InputManager::getMousePosition() const {
    return vec2(currentEventState().mousePosition_);
}

int InputManager::getButtonPressesThisFrame(KeyMouseButtons button) const {
    return m_buttonPressesThisFrame.at(static_cast<int>(button));
}

int InputManager::getButtonReleasesThisFrame(KeyMouseButtons button) const {
    return m_buttonReleasesThisFrame.at(static_cast<int>(button));
}

bool InputManager::hasAnyButtonPressedThisFrame() const {
    for (const int presses : m_buttonPressesThisFrame) {
        if (presses > 0) {
            return true;
        }
    }
    return false;
}

float InputManager::getAxisValue(GamepadAxes axis, int irGamepadId) const {
    return IREntity::getComponent<C_GLFWGamepadState>(m_gamepadEntities.at(irGamepadId))
        .getAxisValue(axis);
}

void InputManager::processKeyMouseButtons(std::queue<int> &queueOfButtons, ButtonStatuses status) {
    while (!queueOfButtons.empty()) {
        int button = queueOfButtons.front();
        KeyMouseButtons irButton = kMapGLFWtoIRKeyMouseButtons.at(button);
        if (status == ButtonStatuses::PRESSED) {
            ++m_buttonPressesThisFrame[irButton];
            for (auto &[event, eventState] : m_eventStates) {
                eventState.pressAccumulator_[static_cast<int>(irButton)] = true;
            }
        }
        if (status == ButtonStatuses::RELEASED) {
            ++m_buttonReleasesThisFrame[irButton];
            for (auto &[event, eventState] : m_eventStates) {
                eventState.releaseAccumulator_[static_cast<int>(irButton)] = true;
            }
        }
        queueOfButtons.pop();

        IRE_LOG_DEBUG("Processed button={}, status={}", button, static_cast<int>(status));
    }
}

void InputManager::processScrolls(std::queue<std::pair<double, double>> &queueOfScrolls) {
    while (!queueOfScrolls.empty()) {
        std::pair<double, double> scroll = queueOfScrolls.front();
        EntityId entityScroll = IREntity::createEntity<kMouseScroll>(scroll.first, scroll.second);
        m_scrollEntitiesThisFrame.push_back(entityScroll);
        queueOfScrolls.pop();

        IRE_LOG_DEBUG("Processed scroll xoffset={}, yoffset={}", scroll.first, scroll.second);
    }
}

void InputManager::initKeyMouseButtonEntities() {
    for (int i = 0; i < KeyMouseButtons::kNumKeyMouseButtons; ++i) {
        EntityId entityNewButton = Prefab<kKeyMouseButton>::create(static_cast<KeyMouseButtons>(i));
        m_keyMouseButtonEntities.insert({static_cast<KeyMouseButtons>(i), entityNewButton});
    }
}

void InputManager::initJoystickEntities() {
    for (int i = GLFW_JOYSTICK_1; i <= GLFW_JOYSTICK_LAST; i++) {
        if (IRWindow::getWindow().joystickPresent(i)) {
            IRE_LOG_INFO("Creating joystick entity for joystick {}", i);

            m_gamepadEntities.emplace_back(
                IREntity::createEntity<kGLFWJoystick>(
                    i,
                    IRWindow::getWindow().getJoystickName(i),
                    IRWindow::getWindow().joystickIsGamepad(i)
                )
            );
        }
    }
}

EventInputState &InputManager::currentEventState() {
    return m_eventStates[m_currentEvent];
}

const EventInputState &InputManager::currentEventState() const {
    return m_eventStates.at(m_currentEvent);
}

} // namespace IRInput