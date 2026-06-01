#ifndef COMMAND_TOGGLE_CULLING_FREEZE_H
#define COMMAND_TOGGLE_CULLING_FREEZE_H

#include <irreden/ir_command.hpp>
#include <irreden/render/cull_viewport_state.hpp>

namespace IRCommand {

// The cull-freeze flag itself lives in IRRender (cull_viewport_state.hpp), next
// to the cull state it gates, so lower layers can drive it without depending on
// the command module (see #1438). This command is the interactive toggle;
// IRCommand::isCullingFrozen() stays as a thin forwarder for existing callers.
inline bool isCullingFrozen() {
    return IRRender::isCullingFrozen();
}

template <> struct Command<TOGGLE_CULLING_FREEZE> {
    static auto create() {
        return []() { IRRender::setCullingFrozen(!IRRender::isCullingFrozen()); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_CULLING_FREEZE_H */
