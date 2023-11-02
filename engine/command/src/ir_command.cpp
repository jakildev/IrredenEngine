#include <irreden/ir_command.hpp>

namespace IRCommand {

    CommandManager* g_commandManager = nullptr;
    CommandManager& getCommandManager() {
        IR_ENG_ASSERT(
            g_commandManager != nullptr,
            "CommandManager not initialized"
        );
        return *g_commandManager;
    }

    template <typename Function
    void registerCommand(
        IRInputTypes inputType,
        int button,
        Function command

    );


} // namespace IRCommand