#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H

#include <irreden/ir_ecs.hpp>
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
        InputManager(IRGLFWWindow& window);
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
        C_MousePosition getMousePositionUpdate() const;
        C_MousePosition getMousePositionRender() const;
        int getButtonPressesThisFrame(KeyMouseButtons button) const;
        int getButtonReleasesThisFrame(KeyMouseButtons button) const;

    private:
        IRGLFWWindow& m_window;
        std::unordered_map<KeyMouseButtons, EntityId> m_keyMouseButtonEntities;
        std::vector<EntityId> m_scrollEntitiesThisFrame;
        std::vector<int> m_buttonPressesThisFrame;
        std::vector<int> m_buttonReleasesThisFrame;
        C_MousePosition m_mousePositionUpdate;
        C_MousePosition m_mousePositionRender;

        void processKeyMouseButtons(
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

                IRProfile::engLogInfo(
                    "Processed button={}, status={}",
                    button,
                    static_cast<int>(status)
                );
            }
        }

        void processScrolls(
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
    };

} // namespace IRInput

#endif /* INPUT_MANAGER_H */
