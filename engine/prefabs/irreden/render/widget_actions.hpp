#ifndef IRREDEN_WIDGET_ACTIONS_H
#define IRREDEN_WIDGET_ACTIONS_H

// Controls as actions. A creation registers each operation once, as an
// `IRCommand::ActionSpec`, and binds the widget that performs it:
//
//   IRCommand::registerAction({.id_ = "editor.tool.erase", .label_ = "ERASE TOOL",
//                              .section_ = "TOOLS", .invoke_ = selectErase});
//   IRPrefab::Widget::bindAction(eraseButton, "editor.tool.erase");
//   ...
//   if (IRPrefab::Widget::wasClicked(eraseButton)) IRCommand::invokeAction("editor.tool.erase");
//
// The click, the chord and a script all run the same `invoke_`. The widget's
// Alt badge shows the action's resolved chord, refreshed by
// `WIDGET_SYNC_ACTION_HINTS` whenever a binding changes, so the badge and the
// Actions window always agree.
//
// Value fields: `registerFocusAction` focuses the field bound to the action and
// `registerStepAction` nudges that field by a fixed step. Both
// resolve the widget when they run, so a panel that is rebuilt or moved keeps
// working as long as it rebinds the new widget.

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_action.hpp>
#include <irreden/render/components/component_widget_hotkey.hpp>
#include <irreden/render/widgets.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace IRPrefab::Widget {

/// Badge text for @p actionId: its first resolved chord, or empty when it is
/// unbound or unknown.
inline std::string actionHint(std::string_view actionId) {
    const std::optional<IRCommand::KeyChord> chord = IRCommand::actions().firstChord(actionId);
    return chord ? IRCommand::chordLabel(*chord) : std::string{};
}

/// Marks @p widget as the control for @p actionId. Replaces any key hotkey the
/// widget had: the action's chords dispatch through `CommandManager`, so a
/// second, widget-level key would fire the operation twice.
inline void bindAction(IREntity::EntityId widget, std::string actionId) {
    std::string hint = actionHint(actionId);
    IREntity::setComponent(widget, IRComponents::C_WidgetAction{std::move(actionId)});
    IREntity::setComponent(
        widget,
        IRComponents::C_WidgetHotkey{IRInput::kNullButton, IRInput::kModifierNone, std::move(hint)}
    );
}

/// The first visible, enabled widget bound to @p actionId, else `kNullEntity`.
/// O(widgets); an invoke-time lookup, never a per-frame one.
inline IREntity::EntityId actionWidget(std::string_view actionId) {
    IREntity::EntityId found = IREntity::kNullEntity;
    IREntity::forEachComponent<IRComponents::C_WidgetAction>(
        [&](IREntity::EntityId &id, IRComponents::C_WidgetAction &action) {
            if (found != IREntity::kNullEntity || action.actionId_ != actionId) {
                return;
            }
            const auto widget = IREntity::getComponentOptional<IRComponents::C_Widget>(id);
            if (widget && (*widget)->visible_ && !(*widget)->disabled_) {
                found = id;
            }
        }
    );
    return found;
}

/// Every widget bound to an action, with its id: the input a discoverability
/// audit groups by section.
inline std::vector<std::pair<IREntity::EntityId, std::string>> actionWidgets() {
    std::vector<std::pair<IREntity::EntityId, std::string>> widgets;
    IREntity::forEachComponent<IRComponents::C_WidgetAction>(
        [&](IREntity::EntityId &id, IRComponents::C_WidgetAction &action) {
            widgets.emplace_back(id, action.actionId_);
        }
    );
    return widgets;
}

/// Registers @p spec with an `invoke_` that focuses the widget bound to its id
/// (`bindAction`), as a click on it would.
inline bool registerFocusAction(IRCommand::ActionSpec spec) {
    spec.invoke_ = [id = spec.id_]() {
        const IREntity::EntityId widget = actionWidget(id);
        if (widget != IREntity::kNullEntity) {
            focus(widget);
        }
    };
    return IRCommand::registerAction(std::move(spec));
}

/// Registers @p spec with an `invoke_` that adds @p step to the slider or
/// number field bound to @p fieldActionId (typically the field's focus action),
/// clamped to the widget's range. One field carries one bound action, so its
/// step actions name it rather than binding it again.
inline bool registerStepAction(IRCommand::ActionSpec spec, std::string fieldActionId, float step) {
    spec.invoke_ = [id = std::move(fieldActionId), step]() {
        const IREntity::EntityId widget = actionWidget(id);
        if (widget == IREntity::kNullEntity) {
            return;
        }
        if (IREntity::getComponentOptional<IRComponents::C_WidgetSlider>(widget)) {
            setSliderValue(widget, sliderValue(widget) + step);
        } else if (IREntity::getComponentOptional<IRComponents::C_WidgetNumberField>(widget)) {
            setNumberFieldValue(widget, numberFieldValue(widget) + step);
        }
    };
    return IRCommand::registerAction(std::move(spec));
}

/// Runs the action bound to @p widget when the widget was clicked this frame.
/// Returns whether it ran.
inline bool invokeIfClicked(IREntity::EntityId widget) {
    if (!wasClicked(widget)) {
        return false;
    }
    const auto action = IREntity::getComponentOptional<IRComponents::C_WidgetAction>(widget);
    return action && IRCommand::invokeAction((*action)->actionId_);
}

} // namespace IRPrefab::Widget

#endif /* IRREDEN_WIDGET_ACTIONS_H */
