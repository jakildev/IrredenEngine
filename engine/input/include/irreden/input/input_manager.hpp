#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <irreden/ir_profile.hpp>

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>
#include <irreden/input/components/component_mouse_position.hpp>
#include <irreden/input/entities/entity_key_mouse_button.hpp>
#include <irreden/time/ir_time_types.hpp>

#include <array>
#include <unordered_map>
#include <queue>

using namespace IRComponents;
using namespace IREntity;

namespace IRInput {

constexpr int kNumTrackedEvents = 3;
constexpr std::array<IRTime::Events, kNumTrackedEvents> kTrackedEvents = {
    IRTime::Events::INPUT,
    IRTime::Events::UPDATE,
    IRTime::Events::RENDER
};

struct EventInputState {
    std::vector<ButtonStatuses> buttonStates_;
    std::vector<bool> pressAccumulator_;
    std::vector<bool> releaseAccumulator_;
    IRMath::dvec2 mousePosition_;

    void resize(int numButtons);
};

class InputManager {
  public:
    InputManager();
    ~InputManager();

    void tick();
    void advanceInputState(IRTime::Events event);

    ButtonStatuses getButtonStatus(KeyMouseButtons button) const;
    bool checkButtonPressed(KeyMouseButtons button) const;
    bool checkButtonDown(KeyMouseButtons button) const;
    bool checkButtonReleased(KeyMouseButtons button) const;
    bool checkButton(KeyMouseButtons button, ButtonStatuses status) const;
    IRMath::vec2 getMousePosition() const;
    int getButtonPressesThisFrame(KeyMouseButtons button) const;
    int getButtonReleasesThisFrame(KeyMouseButtons button) const;
    bool hasAnyButtonPressedThisFrame() const;
    float getAxisValue(GamepadAxes axis, int irGamepadId = 0) const;

  private:
    std::unordered_map<KeyMouseButtons, EntityId> m_keyMouseButtonEntities;
    std::vector<EntityId> m_gamepadEntities;

    std::vector<EntityId> m_scrollEntitiesThisFrame;
    std::vector<int> m_buttonPressesThisFrame;
    std::vector<int> m_buttonReleasesThisFrame;

    std::unordered_map<IRTime::Events, EventInputState> m_eventStates;
    IRTime::Events m_currentEvent = IRTime::Events::INPUT;

    void processKeyMouseButtons(std::queue<int> &queueOfButtons, ButtonStatuses status);

    void processScrolls(std::queue<std::pair<double, double>> &queueOfScrolls);

    void initKeyMouseButtonEntities();
    void initJoystickEntities();

    EventInputState &currentEventState();
    const EventInputState &currentEventState() const;
};

} // namespace IRInput

#endif /* INPUT_MANAGER_H */
