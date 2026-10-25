#ifndef WIDGET_INPUT_TEST_UTILS_H
#define WIDGET_INPUT_TEST_UTILS_H

#include <irreden/command/command_manager.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

namespace WidgetInputTestUtils {

inline void runFrame(
    IRInput::InputManager &inputManager,
    IRSystem::SystemManager &systemManager,
    IRCommand::CommandManager &commandManager
) {
    inputManager.tick();
    inputManager.advanceInputState(IRTime::Events::INPUT);
    systemManager.executePipeline(IRTime::Events::INPUT);
    inputManager.advanceInputState(IRTime::Events::UPDATE);
    commandManager.executeUserKeyboardCommandsAll();
}

} // namespace WidgetInputTestUtils

#endif /* WIDGET_INPUT_TEST_UTILS_H */
