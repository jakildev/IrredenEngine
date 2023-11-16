/*
 * Project: Irreden Engine
 * File: entity_mouse_button.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_KEY_MOUSE_BUTTON_H
#define ENTITY_KEY_MOUSE_BUTTON_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/update/components/component_lifetime.hpp>

using namespace IRComponents;
using namespace IRInput;

namespace IRECS {
    template <>
    struct Prefab<PrefabTypes::kKeyMouseButton> {
        static EntityId create(
            KeyMouseButtons button,
            ButtonStatuses status = ButtonStatuses::NOT_HELD
        )
        {
            return IRECS::createEntity(
                C_KeyMouseButton{button},
                C_KeyStatus{status}
            );
        }
    };

    template <>
    struct Prefab<PrefabTypes::kMouseScroll> {
        static EntityId create(
            double xoffset,
            double yoffset
        )
        {
            return IRECS::createEntity(
                C_MouseScroll{xoffset, yoffset},
                C_Lifetime{1}
            );

        }
    };

} // namespace IRECS


#endif /* ENTITY_KEY_MOUSE_BUTTON_H */
