/*
 * Project: Irreden Engine
 * File: input_manager.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <irreden/ir_profile.hpp>

#include <irreden/input/ir_input_types.hpp>
#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>
#include <irreden/input/components/component_mouse_position.hpp>
#include <irreden/input/entities/entity_key_mouse_button.hpp>

#include <unordered_map>
#include <queue>

using namespace IRComponents;
using namespace IRECS;

namespace IRInput {

    class InputManager {
    public:
        InputManager();
        ~InputManager();

        void tick();
        void tickRender();

        ButtonStatuses getButtonStatus(KeyMouseButtons button) const;
        bool checkButtonPressed(KeyMouseButtons button) const;
        bool checkButtonDown(KeyMouseButtons button) const;
        bool checkButtonReleased(KeyMouseButtons button) const;
        bool checkButton(
            KeyMouseButtons button,
            ButtonStatuses status
        ) const;
        vec2 getMousePositionUpdate() const; // Should be stored in ECS
        vec2 getMousePositionRender() const; // Should be stored in ECS
        int getButtonPressesThisFrame(KeyMouseButtons button) const;
        int getButtonReleasesThisFrame(KeyMouseButtons button) const;
        float getAxisValue(
            GamepadAxes axis,
            int irGamepadId = 0
        ) const;

    private:
        std::unordered_map<KeyMouseButtons, EntityId> m_keyMouseButtonEntities;
        std::vector<EntityId> m_gamepadEntities;

        std::vector<EntityId> m_scrollEntitiesThisFrame;
        std::vector<int> m_buttonPressesThisFrame;
        std::vector<int> m_buttonReleasesThisFrame;
        dvec2 m_mousePositionUpdate;
        dvec2 m_mousePositionRender;

        void processKeyMouseButtons(
            std::queue<int>& queueOfButtons,
            ButtonStatuses status
        );

        void processScrolls(
            std::queue<std::pair<double, double>>& queueOfScrolls
        );

        void initKeyMouseButtonEntities();
        void initJoystickEntities();
    };

} // namespace IRInput

#endif /* INPUT_MANAGER_H */
