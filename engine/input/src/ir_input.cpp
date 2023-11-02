#include <irreden/ir_input.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>


namespace IRInput {

    bool checkKeyMouseButton(
        KeyMouseButtons button,
        ButtonStatuses checkStatus
    )
    {
        return IRECS::getSystem<IRECS::SystemName::INPUT_KEY_MOUSE>().
            checkButton(
                button,
                checkStatus
            );
    }
} // namespace IRInput