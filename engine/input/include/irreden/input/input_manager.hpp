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
#include <utility>

using namespace IRComponents;
using namespace IREntity;

namespace IRInput {

constexpr int kNumTrackedEvents = 3;
constexpr std::array<IRTime::Events, kNumTrackedEvents> kTrackedEvents = {
    IRTime::Events::INPUT, IRTime::Events::UPDATE, IRTime::Events::RENDER
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
    bool checkGamepadButton(GamepadButtons button, ButtonStatuses status, int irGamepadId = 0) const;
    float getGamepadAxis(GamepadAxes axis, int irGamepadId = 0) const;

    // Synthetic input (#1794): deterministic injection so a headless run can
    // drive the cursor and buttons without GLFW — the missing primitive the
    // GUI/mouse verification harness (#1793) depends on. `beginSyntheticInput`
    // flips the manager to consume injected events for the rest of the run,
    // mirroring `IRVideo::isAutoCaptureActive()`'s run-scoped fixed-step flip.
    // When inactive the GLFW path stays byte-identical to today.
    void beginSyntheticInput();
    bool isSyntheticInputActive() const {
        return m_syntheticInputActive;
    }
    // Set the cursor (screen pixels, GLFW's space) for the next frame's snapshot.
    void injectMouseMove(IRMath::ivec2 screenPx);
    // Enqueue a button press/release, applied at the next frame boundary.
    void injectButton(KeyMouseButtons button, ButtonStatuses status);
    // Enqueue a scroll delta, applied at the next frame boundary.
    void injectScroll(double xOffset, double yOffset);

  private:
    std::unordered_map<KeyMouseButtons, EntityId> m_keyMouseButtonEntities;
    std::vector<EntityId> m_gamepadEntities;

    std::vector<EntityId> m_scrollEntitiesThisFrame;
    std::vector<int> m_buttonPressesThisFrame;
    std::vector<int> m_buttonReleasesThisFrame;

    std::unordered_map<IRTime::Events, EventInputState> m_eventStates;
    IRTime::Events m_currentEvent = IRTime::Events::INPUT;

    // Synthetic-input state (see the public inject API above). When
    // `m_syntheticInputActive` is set, `tick()` drains these injected queues
    // instead of the GLFW window queues and `advanceInputState` snapshots
    // `m_syntheticCursorScreen` instead of the live GLFW cursor. The pending
    // cursor is latched into the current one once per `tick()` so every
    // pipeline-event snapshot in a frame reads the same position.
    bool m_syntheticInputActive = false;
    IRMath::dvec2 m_syntheticCursorScreen{0.0, 0.0};
    IRMath::dvec2 m_syntheticCursorPending{0.0, 0.0};
    std::queue<std::pair<KeyMouseButtons, ButtonStatuses>> m_injectedButtons;
    std::queue<std::pair<double, double>> m_injectedScrolls;

    void processKeyMouseButtons(std::queue<int> &queueOfButtons, ButtonStatuses status);

    void processScrolls(std::queue<std::pair<double, double>> &queueOfScrolls);

    // Records one button event into the per-event accumulators + frame counters.
    // Shared by the GLFW drain (`processKeyMouseButtons`) and synthetic injection.
    void applyButtonEvent(KeyMouseButtons button, ButtonStatuses status);

    // Drains the injected queues into the same per-event state the GLFW path
    // writes; the synthetic counterpart of the `tick()` GLFW drain.
    void drainInjectedInput();

    void initKeyMouseButtonEntities();
    void initJoystickEntities();

    EventInputState &currentEventState();
    const EventInputState &currentEventState() const;
};

} // namespace IRInput

#endif /* INPUT_MANAGER_H */
