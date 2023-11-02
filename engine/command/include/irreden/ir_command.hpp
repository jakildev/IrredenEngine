#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/input/ir_input_types.hpp>
#include <irreden/command/command_manager.hpp>

namespace IRCommand {

    extern CommandManager* g_commandManager;
    CommandManager& getCommandManager();

    template <typename Function>
    int registerCommand(
        IRInput::InputTypes inputType,
        IRInput::ButtonStatuses triggerStatus,
        int button,
        Function command
    )
    {
        return getCommandManager().registerCommand(
            inputType,
            triggerStatus,
            button,
            command
        );
    }

} // namespace IRCommand

#endif /* IR_COMMAND_H */
