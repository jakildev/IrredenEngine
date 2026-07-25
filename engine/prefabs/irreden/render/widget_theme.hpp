#ifndef WIDGET_THEME_H
#define WIDGET_THEME_H

#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_widget_theme.hpp>

namespace IRPrefab::Widget {

// Default theme accessor, backed by the `C_WidgetTheme` singleton
// component. A creation that wants custom colors may mutate this once at
// init before the first frame — setup-time only; per-tick readers
// must cache the returned reference in `beginTick` rather than calling
// this per entity (the ECS footgun).
inline WidgetTheme &defaultTheme() {
    return IREntity::singleton<IRComponents::C_WidgetTheme>().theme_;
}

// Registration-time singleton touch. Every theme-reading widget system
// calls this from `create()` so the lazy-create (a `createEntity`) runs on
// the main thread during pipeline wiring, before frame 1 — never inside a
// first-frame `beginTick`. `singleton<T>()`'s first call must be
// main-thread: a worker-thread `createEntity` defers the row insertion
// until `flushStructuralChanges`, so the immediately-following
// `getComponent` would read a row that doesn't exist yet.
inline void ensureThemeSingleton() {
    defaultTheme();
}

} // namespace IRPrefab::Widget

#endif /* WIDGET_THEME_H */
