#ifndef COMMAND_TOGGLE_PERF_HUD_H
#define COMMAND_TOGGLE_PERF_HUD_H

#include <irreden/ir_command.hpp>

#include <irreden/render/perf_hud_state.hpp>

namespace IRCommand {

template <> struct Command<TOGGLE_PERF_HUD> {
    static auto create() {
        return []() { IRPrefab::PerfHud::toggleVisible(); };
    }
};

} // namespace IRCommand

#endif /* COMMAND_TOGGLE_PERF_HUD_H */
