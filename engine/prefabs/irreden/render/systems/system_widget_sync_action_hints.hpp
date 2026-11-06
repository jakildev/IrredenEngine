#ifndef SYSTEM_WIDGET_SYNC_ACTION_HINTS_H
#define SYSTEM_WIDGET_SYNC_ACTION_HINTS_H

#include <irreden/ir_command.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_widget_action.hpp>
#include <irreden/render/components/component_widget_hotkey.hpp>
#include <irreden/render/widget_actions.hpp>

#include <cstdint>

namespace IRSystem {

// Rewrites the badge text of every action-bound widget after an action's chords
// change. The registry generation is the only input the badge reads besides the
// widget's own action id, which `bindAction` resolves when it is set; a frame
// with an unchanged generation therefore has nothing to recompute, and its cost
// is one integer compare.
template <> struct System<WIDGET_SYNC_ACTION_HINTS> {
    std::uint32_t syncedGeneration_ = ~std::uint32_t{0};
    bool stale_ = false;

    void beginTick() {
        const std::uint32_t generation = IRCommand::actions().generation();
        stale_ = generation != syncedGeneration_;
        syncedGeneration_ = generation;
    }

    void tick(const IRComponents::C_WidgetAction &action, IRComponents::C_WidgetHotkey &hotkey) {
        if (stale_) {
            hotkey.hint_ = IRPrefab::Widget::actionHint(action.actionId_);
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_SYNC_ACTION_HINTS,
            IRComponents::C_WidgetAction,
            IRComponents::C_WidgetHotkey>("WidgetSyncActionHints");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_SYNC_ACTION_HINTS_H */
