#ifndef IR_PREFAB_ACTIONS_WINDOW_H
#define IR_PREFAB_ACTIONS_WINDOW_H

// Adoption surface for the Actions window and persistent user keymaps.
//
//   // initSystems() — INPUT, after WIDGET_INPUT and the WIDGET_APPLY_* chain
//   // (WIDGET_APPLY_LIST and WIDGET_APPLY_TEXT_INPUT at least):
//   inputPipeline.splice(inputPipeline.end(), IRPrefab::ActionsWindow::inputSystems());
//   // RENDER: the panel, label, button, list and text-input renderers, then
//   // WIDGET_RENDER_HINTS for the Alt badges of action-bound controls.
//
//   // initCommands() — after the creation's own commands and actions:
//   IRCommand::actions().adoptNamedCommands("mycreation");   // optional
//   IRPrefab::ActionsWindow::registerToggleAction();
//   IRPrefab::ActionsWindow::attachUserKeymap("mycreation");
//
// The window's widgets paint in the ordinary widget passes, which order by
// kind, not by z: place it (`Params::origin_`) over GUI space no other widget
// occupies, or another panel's labels and buttons draw over it.
//
// The keymap lives at `<preferences>/keymaps/<creation>.keymap`
// (`IRCommand::keymapPathFor`). A scripted run (`--auto-screenshot`, a GUI
// test) only reads or writes it when `IR_PREFS_DIR` names a directory, so a
// replay never touches a person's real bindings.

#include <irreden/ir_command.hpp>
#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/command/keymap_store.hpp>
#include <irreden/render/actions_window_layout.hpp>
#include <irreden/render/components/component_actions_window.hpp>
#include <irreden/render/prefab_system_probe.hpp>
#include <irreden/render/systems/system_actions_window.hpp>
#include <irreden/render/systems/system_widget_sync_action_hints.hpp>
#include <irreden/utility/path_utils.hpp>

#include <list>
#include <optional>
#include <string>
#include <vector>

namespace IRPrefab::ActionsWindow {

inline constexpr std::string_view kToggleActionId = "engine.actions.window";

/// Shift+/ ("?"), the key an Actions list conventionally opens on.
inline constexpr IRCommand::KeyChord kDefaultToggleChord{
    IRInput::kKeyButtonSlash, IRInput::kModifierShift
};

inline IRComponents::C_ActionsWindowState &state() {
    return IREntity::singleton<IRComponents::C_ActionsWindowState>();
}

inline bool isOpen() {
    const auto *window = IREntity::singletonOrNull<IRComponents::C_ActionsWindowState>();
    return window != nullptr && window->open_;
}

inline void setOpen(bool open) {
    state().open_ = open;
}

/// `ACTIONS_WINDOW` plus `WIDGET_SYNC_ACTION_HINTS`, for splicing after the
/// creation's `WIDGET_APPLY_*` chain.
inline std::list<IRSystem::SystemId>
inputSystems(const IRSystem::System<IRSystem::ACTIONS_WINDOW>::Params &params = {}) {
    return {
        IRSystem::System<IRSystem::WIDGET_SYNC_ACTION_HINTS>::create(),
        IRSystem::System<IRSystem::ACTIONS_WINDOW>::create(params),
    };
}

/// Registers the action that opens and closes the window.
inline bool
registerToggleAction(std::vector<IRCommand::KeyChord> defaults = {kDefaultToggleChord}) {
    state();
    IRCommand::ActionSpec spec;
    spec.id_ = std::string(kToggleActionId);
    spec.label_ = "ACTIONS WINDOW";
    spec.section_ = "GENERAL";
    spec.description_ = "SEARCH, RUN AND ASSIGN KEYS TO ACTIONS";
    spec.defaultChords_ = std::move(defaults);
    spec.invoke_ = []() { state().open_ = !state().open_; };
    return IRCommand::registerAction(std::move(spec));
}

/// Whether this run may touch on-disk preferences: always for an interactive
/// run, and for a scripted one only under an explicit `IR_PREFS_DIR`.
inline bool userPreferencesAllowed() {
    const bool scripted = IREngine::args().autoScreenshotWarmupFrames() > 0 ||
                          IREngine::args().autoRecordFrames() > 0;
    return !scripted || !IRUtility::envString(IRCommand::kPreferencesDirEnv).empty();
}

/// Attaches `<preferences>/keymaps/<creation>.keymap` to the action registry:
/// loads the user's overrides now and saves each later change. Returns nullopt
/// when `userPreferencesAllowed()` says this run must not touch preferences.
inline std::optional<IRCommand::KeymapLoadResult> attachUserKeymap(const std::string &creation) {
    if (!userPreferencesAllowed()) {
        return std::nullopt;
    }
    return IRCommand::keymap().attach(IRCommand::keymapPathFor(creation), creation);
}

// --- Headless-test introspection -------------------------------------------

inline const IRSystem::System<IRSystem::ACTIONS_WINDOW> *systemOrNull() {
    return IRPrefab::detail::systemOrNull<IRSystem::ACTIONS_WINDOW>();
}

/// Ids of the listed rows, top to bottom; empty while closed.
inline std::vector<std::string> listedActionIds() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::vector<std::string>{} : system->rowIds_;
}

/// Full (untruncated) text of each listed row.
inline std::vector<std::string> listedRowText() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::vector<std::string>{} : system->rowText_;
}

inline std::string selectedActionId() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::string{} : system->selectedId_;
}

inline std::string statusText() {
    const auto *system = systemOrNull();
    return system == nullptr ? std::string{} : system->status_;
}

/// The layout the window builds at the creation's configured origin.
inline std::optional<Layout> builtLayout() {
    const auto *system = systemOrNull();
    if (system == nullptr || !system->built_) {
        return std::nullopt;
    }
    return system->layout_;
}

} // namespace IRPrefab::ActionsWindow

#endif /* IR_PREFAB_ACTIONS_WINDOW_H */
