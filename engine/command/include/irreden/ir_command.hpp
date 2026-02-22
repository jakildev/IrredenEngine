#ifndef IR_COMMAND_H
#define IR_COMMAND_H

#include <irreden/ir_input.hpp>

#include <irreden/command/ir_command_types.hpp>
#include <irreden/command/command_manager.hpp>

namespace IRCommand {

extern CommandManager *g_commandManager;
CommandManager &getCommandManager();

template <typename Function>
int createCommand(
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    Function command,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone
) {
    return getCommandManager().createCommand(
        inputType,
        triggerStatus,
        button,
        command,
        requiredModifiers,
        blockedModifiers
    );
}

template <CommandNames commandName>
CommandId createCommand(
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    IRInput::KeyModifierMask requiredModifiers = IRInput::kModifierNone,
    IRInput::KeyModifierMask blockedModifiers = IRInput::kModifierNone
) {
    return getCommandManager().createCommand(
        inputType,
        triggerStatus,
        button,
        Command<commandName>::create(),
        requiredModifiers,
        blockedModifiers
    );
}

} // namespace IRCommand

#endif /* IR_COMMAND_H */
