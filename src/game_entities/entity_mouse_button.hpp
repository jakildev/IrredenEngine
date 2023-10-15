/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_entities\entity_mouse_button.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_MOUSE_BUTTON_H
#define ENTITY_MOUSE_BUTTON_H

#include "../entity/prefabs.hpp"
#include "../entity/entity_handle.hpp"
#include "../game_components/component_tags_all.hpp"
#include "../game_components/component_mouse_button.hpp"
#include "../game_components/component_keyboard_key_status.hpp"
#include "../game_components/component_lifetime.hpp"
#include "../input/ir_input.hpp"

using namespace IRComponents;
using namespace IRInput;

namespace IRECS {
    template <>
    struct Prefab<PrefabTypes::kKeyMouseButton> {
        static EntityHandle create(
            IRKeyMouseButtons button,
            IRButtonStatuses status = IRButtonStatuses::kNotHeld
        )
        {
            EntityHandle entity{};
            entity.set(C_KeyMouseButton{button});
            entity.set(C_KeyStatus{status});
            return entity;
        }
    };

    template <>
    struct Prefab<PrefabTypes::kMouseScroll> {
        static EntityHandle create(
            double xoffset,
            double yoffset
        )
        {
            EntityHandle entity{};
            entity.set(C_MouseScroll{xoffset, yoffset});
            entity.set(C_Lifetime{1});

            return entity;
        }
    };

} // namespace IRECS


#endif /* ENTITY_MOUSE_BUTTON_H */
