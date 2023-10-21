/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_input_key_mouse.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_INPUT_KEY_MOUSE_H
#define SYSTEM_INPUT_KEY_MOUSE_H

#include "..\world\global.hpp"
#include "..\ecs\ir_system_base.hpp"
#include "..\world\ir_constants.hpp"
#include "..\input\ir_glfw_window.hpp"

#include "..\entities\entity_mouse_button.hpp"

#include "..\components\component_tags_all.hpp"
#include "..\components\component_mouse_position.hpp"

using namespace IRComponents;
using namespace IRMath;
using namespace IRECS;
using IRGLFW::IRGLFWWindow;

namespace IRECS {

    template <>
    class IRSystem<INPUT_KEY_MOUSE> : public IRSystemBase<
        INPUT_KEY_MOUSE,
        C_KeyStatus,
        C_KeyMouseButton
    > {
    public:
        IRSystem(IRGLFWWindow& window)
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

            for(int i = 0; i < IRKeyMouseButtons::kNumKeyMouseButtons; ++i) {
                EntityHandle entityNewButton = Prefab<kKeyMouseButton>::create(
                    static_cast<IRKeyMouseButtons>(i)
                );
                m_keyMouseButtonEntities.insert({
                    static_cast<IRKeyMouseButtons>(i),
                    entityNewButton
                });
            }
            ENG_LOG_INFO("Created system INPUT_KEY_MOUSE");
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
        for(int i=0; i < entities.size(); ++i) {
            C_KeyStatus& componentStatus = keyStatuses[i];
            int mouseButtonPressCount = m_buttonPressesThisFrame.at(
                mouseButtons[i].button_
            );
            int mouseButtonReleaseCount = m_buttonReleasesThisFrame.at(
                mouseButtons[i].button_
            );
            if(componentStatus.status_ == kPressed) {
                componentStatus.status_ = kHeld;
            }
            else if(componentStatus.status_ == kReleased) {
                componentStatus.status_ = kNotHeld;
            }

            if(mouseButtonPressCount > 0 && mouseButtonReleaseCount <= 0) {
                componentStatus.status_ = kPressed;
            }
            else if(mouseButtonPressCount <= 0 && mouseButtonReleaseCount > 0) {
                componentStatus.status_ = kReleased;
            }
            else if(mouseButtonPressCount > 0 && mouseButtonReleaseCount > 0) {
                componentStatus.status_ = kPressedAndReleased;
            }
            componentStatus.pressedThisFrameCount_ = mouseButtonPressCount;
            componentStatus.releasedThisFrameCount_ = mouseButtonReleaseCount;
        }
    }


    C_MousePosition getMousePositionUpdate() const {
        return m_mousePositionUpdate;
    }

    C_MousePosition getMousePositionRender() const {
        return m_mousePositionRender;
    }

    bool checkButton(IRInputTypes inputType, IRKeyMouseButtons button) const {
        if(inputType == IRInputTypes::kKeyMouseButtonPressed) {
            return checkButtonPressed(button);
        }
        if(inputType == IRInputTypes::kKeyMouseButtonReleased) {
            return checkButtonReleased(button);
        }
        if(inputType == IRInputTypes::kKeyMouseButtonDown) {
            return checkButtonDown(button);
        }
        return false;
    }

    bool checkButtonPressed(IRKeyMouseButtons button) const {
        if(m_keyMouseButtonEntities.at(button).get<C_KeyStatus>().pressedThisFrameCount_ > 0) {
            return true;
        }
        return false;
    }

    // Intentionally not using the kPressedAndReleased status for this
    bool checkButtonDown(IRKeyMouseButtons button) const {
        const C_KeyStatus buttonStatus =
            m_keyMouseButtonEntities.at(button).get<C_KeyStatus>();
        if(
            buttonStatus.status_ == IRButtonStatuses::kPressed ||
            buttonStatus.status_ == IRButtonStatuses::kHeld
        )
        {
            return true;
        }
        return false;
    }

    bool checkButtonReleased(IRKeyMouseButtons button) const {
        if(m_keyMouseButtonEntities.at(button).get<C_KeyStatus>().releasedThisFrameCount_ > 0) {
            return true;
        }
        return false;
    }


    private:
        IRGLFW::IRGLFWWindow& m_window;
        std::vector<EntityId> m_scrollEntitiesThisFrame;
        std::vector<int> m_buttonPressesThisFrame;
        std::vector<int> m_buttonReleasesThisFrame;
        std::unordered_map<IRKeyMouseButtons, EntityHandle> m_keyMouseButtonEntities;
        EntityHandle m_entityMouse;

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
            processKeyMouseButtons(m_window.getKeysPressedToProcess(), IRButtonStatuses::kPressed);
            processKeyMouseButtons(m_window.getKeysReleasedToProcess(), IRButtonStatuses::kReleased);
            processKeyMouseButtons(m_window.getMouseButtonsPressedToProcess(), IRButtonStatuses::kPressed);
            processKeyMouseButtons(m_window.getMouseButtonsReleasedToProcess(), IRButtonStatuses::kReleased);
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
            IRButtonStatuses status
        )
        {
            while(!queueOfButtons.empty()) {
                int button = queueOfButtons.front();
                IRKeyMouseButtons irButton = kMapGLFWtoIRKeyMouseButtons.at(button);
                if(status == IRButtonStatuses::kPressed) {
                    ++m_buttonPressesThisFrame[irButton];
                }
                if(status == IRButtonStatuses::kReleased) {
                    ++m_buttonReleasesThisFrame[irButton];

                }
                queueOfButtons.pop();

                ENG_LOG_DEBUG(
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
                EntityHandle entityScroll =
                    Prefab<kMouseScroll>::create(scroll.first, scroll.second);
                m_scrollEntitiesThisFrame.push_back(entityScroll.id_);
                queueOfScrolls.pop();

                ENG_LOG_DEBUG(
                    "Processed scroll xoffset={}, yoffset={}",
                    scroll.first,
                    scroll.second
                );
            }
        }
    };

} // namespace IRSystem

#endif /* SYSTEM_INPUT_KEY_MOUSE_H */
