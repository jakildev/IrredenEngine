#ifndef WIDGET_THEME_H
#define WIDGET_THEME_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_widget_theme.hpp>

namespace IRPrefab::Widget {

// Default theme accessor, backed by the `C_WidgetTheme` singleton
// component. A creation that wants custom colors mutates this once at
// init before constructing widgets — setup-time only; per-tick readers
// must cache the returned reference in `beginTick` rather than calling
// this per entity (the ECS footgun).
inline WidgetTheme &defaultTheme() {
    return IREntity::singleton<IRComponents::C_WidgetTheme>().theme_;
}

} // namespace IRPrefab::Widget

#endif /* WIDGET_THEME_H */
