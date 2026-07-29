#ifndef COMMAND_TOGGLE_HELP_OVERLAY_H
#define COMMAND_TOGGLE_HELP_OVERLAY_H

#include <irreden/ir_command.hpp>

#include <irreden/render/help_overlay_state.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_HELP_OVERLAY> {
    static auto create() {
        return []() { IRPrefab::HelpOverlay::toggleVisible(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_HELP_OVERLAY_H */
