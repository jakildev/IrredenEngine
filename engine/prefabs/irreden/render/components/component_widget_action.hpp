#ifndef COMPONENT_WIDGET_ACTION_H
#define COMPONENT_WIDGET_ACTION_H

#include <string>

namespace IRComponents {

// The action a control performs (`IRCommand::ActionSpec::id_`). Attached by
// `IRPrefab::Widget::bindAction`, which also gives the widget a keyless
// `C_WidgetHotkey` whose hint is the action's resolved chord, so the Alt badge
// and the Actions window read the same binding. A keyless hotkey never
// dispatches; the chord fires the action through `CommandManager` instead.
struct C_WidgetAction {
    std::string actionId_;
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_ACTION_H */
