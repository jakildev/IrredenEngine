#ifndef WIDGET_HINTS_H
#define WIDGET_HINTS_H

#include <irreden/ir_system.hpp>
#include <irreden/render/systems/system_widget_render_hints.hpp>

namespace IRPrefab::Widget {

inline const IRSystem::System<IRSystem::WIDGET_RENDER_HINTS> *hintSystemOrNull() {
    const IRSystem::SystemId id = IRSystem::findSystem(IRSystem::WIDGET_RENDER_HINTS);
    if (id == IRSystem::kNullSystemId) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::WIDGET_RENDER_HINTS>>(id);
}

inline std::size_t lastHintGlyphCommandCount() {
    const auto *system = hintSystemOrNull();
    return system == nullptr ? 0 : system->lastHintGlyphCommandCount_;
}

inline IREntity::EntityId lastFocusRingWidget() {
    const auto *system = hintSystemOrNull();
    return system == nullptr ? IREntity::kNullEntity : system->lastFocusRingWidget_;
}

} // namespace IRPrefab::Widget

#endif /* WIDGET_HINTS_H */
