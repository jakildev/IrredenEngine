#ifndef SYSTEM_WIDGET_APPLY_RADIO_H
#define SYSTEM_WIDGET_APPLY_RADIO_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_widget.hpp>

#include <cstdint>
#include <vector>

namespace IRSystem {

// Per-kind follower for radio buttons. Click-release (fireAction) sets
// this radio's selected_ flag; group exclusion (clearing every sibling
// that shares groupId_) runs in endTick so we don't nest a forEach
// inside another forEach. Tick collects (groupId, firedEntityId) into
// a scratch vector; endTick consumes it and walks the radios once.
template <> struct System<WIDGET_APPLY_RADIO> {
    struct FiredRadio {
        IREntity::EntityId entity_ = IREntity::kNullEntity;
        std::uint32_t groupId_ = 0;
    };

    std::vector<FiredRadio> firedThisFrame_;
    bool reserved_ = false;

    void beginTick() {
        if (!reserved_) {
            firedThisFrame_.reserve(8);
            reserved_ = true;
        }
        firedThisFrame_.clear();
    }

    void tick(
        IREntity::EntityId entityId,
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetState &state,
        IRComponents::C_WidgetRadio &radio
    ) {
        if (widget.disabled_) return;
        if (!state.fireAction_) return;
        radio.selected_ = true;
        firedThisFrame_.push_back({entityId, radio.groupId_});
    }

    void endTick() {
        if (firedThisFrame_.empty()) return;
        IREntity::forEachComponent<IRComponents::C_WidgetRadio>(
            [this](IREntity::EntityId &id, IRComponents::C_WidgetRadio &radio) {
                for (const auto &fired : firedThisFrame_) {
                    if (fired.entity_ == id) continue;
                    if (fired.groupId_ == radio.groupId_) {
                        radio.selected_ = false;
                    }
                }
            }
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_APPLY_RADIO,
            IRComponents::C_Widget,
            IRComponents::C_WidgetState,
            IRComponents::C_WidgetRadio
        >("WidgetApplyRadio");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_APPLY_RADIO_H */
