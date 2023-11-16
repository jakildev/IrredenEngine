/*
 * Project: Irreden Engine
 * File: system_input_key_mouse.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_KEY_MOUSE_H
#define SYSTEM_INPUT_KEY_MOUSE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/input/entities/entity_key_mouse_button.hpp>
#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>
#include <irreden/input/components/component_mouse_position.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRInput;

namespace IRECS {

    template <>
    class System<INPUT_KEY_MOUSE> : public SystemBase<
        INPUT_KEY_MOUSE,
        C_KeyStatus,
        C_KeyMouseButton
    > {
    public:
        System(IRGLFWWindow& window)
        :   m_window{window}
        ,   m_scrollEntitiesThisFrame{}
        ,   m_buttonPressesThisFrame{}
        ,   m_buttonReleasesThisFrame{}
        ,   m_entityMouse{}
        ,   m_mousePositionUpdate{}
        ,   m_mousePositionRender{}
        {

            m_buttonPressesThisFrame.resize(kNumKeyMouseButtons);
            m_buttonReleasesThisFrame.resize(kNumKeyMouseButtons);

            for(int i = 0; i < KeyMouseButtons::kNumKeyMouseButtons; ++i) {
                EntityId entityNewButton = Prefab<kKeyMouseButton>::create(
                    static_cast<KeyMouseButtons>(i)
                );
                m_keyMouseButtonEntities.insert({
                    static_cast<KeyMouseButtons>(i),
                    entityNewButton
                });
            }
            IRProfile::engLogInfo("Created system INPUT_KEY_MOUSE");
        }

        // TEMP
        void beginRenderExecute() {
            m_window.getUpdateCursorPos(
                m_mousePositionRender.pos_.x,
                m_mousePositionRender.pos_.y
            );


        }

    void tickWithArchetype(
        Archetype type,
        std::vector<EntityId>& entities,
        std::vector<C_KeyStatus>& keyStatuses,
        std::vector<C_KeyMouseButton>& mouseButtons
    )
    {
        // Shouldn't the entities get updated here?
        for(int i=0; i < entities.size(); ++i) {
            C_KeyStatus& componentStatus = keyStatuses[i];
            int mouseButtonPressCount = m_buttonPressesThisFrame.at(
                mouseButtons[i].button_
            );
            int mouseButtonReleaseCount = m_buttonReleasesThisFrame.at(
                mouseButtons[i].button_
            );
            if(componentStatus.status_ == PRESSED) {
                componentStatus.status_ = HELD;
            }
            else if(componentStatus.status_ == RELEASED) {
                componentStatus.status_ = NOT_HELD;
            }

            if(mouseButtonPressCount > 0 && mouseButtonReleaseCount <= 0) {
                componentStatus.status_ = PRESSED;
            }
            else if(mouseButtonPressCount <= 0 && mouseButtonReleaseCount > 0) {
                componentStatus.status_ = RELEASED;
            }
            else if(mouseButtonPressCount > 0 && mouseButtonReleaseCount > 0) {
                componentStatus.status_ = PRESSED_AND_RELEASED;
            }
            componentStatus.pressedThisFrameCount_ = mouseButtonPressCount;
            componentStatus.releasedThisFrameCount_ = mouseButtonReleaseCount;
        }
    }

    ButtonStatuses getButtonStatus(KeyMouseButtons button) const {
        return IRECS::getComponent<C_KeyStatus>(
            m_keyMouseButtonEntities.at(button)
        ).status_;
    }


    C_MousePosition getMousePositionUpdate() const {
        return m_mousePositionUpdate;
    }

    C_MousePosition getMousePositionRender() const {
        return m_mousePositionRender;
    }

    bool checkButton(KeyMouseButtons button, ButtonStatuses status) const {
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

    bool checkButtonPressed(KeyMouseButtons button) const {
        return getButtonStatus(button) == ButtonStatuses::PRESSED ||
            getButtonStatus(button) == ButtonStatuses::PRESSED_AND_RELEASED;
    }

    // Intentionally not using the PRESSED_AND_RELEASED status for this
    bool checkButtonDown(KeyMouseButtons button) const {
        return getButtonStatus(button) == ButtonStatuses::PRESSED ||
            getButtonStatus(button) == ButtonStatuses::HELD;
    }

    bool checkButtonReleased(KeyMouseButtons button) const {
        return getButtonStatus(button) == ButtonStatuses::RELEASED ||
            getButtonStatus(button) == ButtonStatuses::PRESSED_AND_RELEASED;
    }

    private:
        IRInput::IRGLFWWindow& m_window;
        std::vector<EntityId> m_scrollEntitiesThisFrame;
        std::vector<int> m_buttonPressesThisFrame;
        std::vector<int> m_buttonReleasesThisFrame;
        std::unordered_map<KeyMouseButtons, EntityId> m_keyMouseButtonEntities;
        EntityId m_entityMouse;

        C_MousePosition m_mousePositionUpdate;
        C_MousePosition m_mousePositionRender;
        // C_MousePosition m_entityMousePositionOnDemand;

        virtual void beginExecute() override {
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

            // TODO: Process buttons to match new component design...
            processKeyMouseButtons(m_window.getKeysPressedToProcess(), ButtonStatuses::PRESSED);
            processKeyMouseButtons(m_window.getKeysReleasedToProcess(), ButtonStatuses::RELEASED);
            processKeyMouseButtons(m_window.getMouseButtonsPressedToProcess(), ButtonStatuses::PRESSED);
            processKeyMouseButtons(m_window.getMouseButtonsReleasedToProcess(), ButtonStatuses::RELEASED);
            processScrolls(m_window.getScrollsToProcess());

            m_window.getUpdateCursorPos(
                m_mousePositionUpdate.pos_.x,
                m_mousePositionUpdate.pos_.y
            );

        }
        virtual void endExecute() override {

        }


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

                IRProfile::engLogDebug(
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

} // namespace System

#endif /* SYSTEM_INPUT_KEY_MOUSE_H */
