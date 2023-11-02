#include <irreden/ir_command.hpp>
#include <irreden/ir_profile.hpp>

namespace IRCommand {

    CommandManager* g_commandManager = nullptr;
    CommandManager& getCommandManager() {
        IR_ASSERT(
            g_commandManager != nullptr,
            "CommandManager not initialized"
        );
        return *g_commandManager;
    }


} // namespace IRCommand