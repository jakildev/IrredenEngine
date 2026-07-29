#ifndef IR_PREFAB_SETTINGS_MENU_H
#define IR_PREFAB_SETTINGS_MENU_H

// Adoption surface for the standard pause/settings menu (#2551).
//
// A creation registers what is togglable and wires three lists; the menu owns
// layout, interaction, and teardown:
//
//   // initSystems() — INPUT, after the creation's own input systems:
//   inputPipeline.splice(inputPipeline.end(), IRPrefab::SettingsMenu::inputSystems());
//
//   // initSystems() — RENDER, after TEXT_TO_TRIXEL, before the composite:
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>());
//   renderPipeline.splice(renderPipeline.end(), IRPrefab::SettingsMenu::renderSystems());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
//
//   // initCommands() — Escape opens the menu, so the camera suite must not
//   // also bind it to quit; the menu's QUIT button replaces that path:
//   IRPrefab::Camera::registerStandardKeyboardCommands(/*bindEscapeCloseWindow=*/false);
//   IRPrefab::SettingsMenu::registerToggleCommand();
//
//   // initEntities() — one call per togglable mode:
//   IRPrefab::Settings::registerBool("CHECKERBOARD", getter, setter);
//
// Settings may be registered at any point before the menu is first opened;
// the menu reads the registry when it opens, so a late registration appears
// at the next open rather than mid-session.

#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/settings_registry.hpp>
#include <irreden/render/commands/command_toggle_settings_menu.hpp>
#include <irreden/render/components/component_settings_menu.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/render/systems/system_settings_menu.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_render_button.hpp>
#include <irreden/render/systems/system_widget_render_checkbox.hpp>
#include <irreden/render/systems/system_widget_render_dropdown.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/systems/system_widget_render_panel.hpp>
#include <irreden/render/systems/system_widget_render_slider.hpp>

#include <list>

namespace IRPrefab::SettingsMenu {

// Escape. Repurposing it is the reason `registerCameraCommands` grew an
// opt-out: every demo historically quit on Escape, and a pause menu that
// opens on anything else is not the key people reach for. Quitting stays
// reachable through the menu's QUIT button.
inline constexpr int kDefaultToggleButton = IRInput::KeyMouseButtons::kKeyButtonEscape;

// Splice-ready INPUT-pipeline systems.
//
// **Precondition: `INPUT_KEY_MOUSE` must already be in this creation's INPUT
// pipeline, ahead of where this list is spliced** — the hover test consumes
// the cursor state it produces. Every creation that takes keyboard input
// already registers it.
//
// The widget chain is ordered hover-test → state machine → per-kind apply →
// menu poll, matching `ui_widgets`. Only the three widget kinds the menu
// actually spawns get an apply system; a creation that wants the rest adds
// them itself. `SETTINGS_MENU` runs last so it polls state this frame's
// clicks already produced rather than last frame's.
//
// Like `HelpOverlay::systems()`, this deliberately does not auto-detect what
// the creation already registered: a duplicate `WIDGET_INPUT` would run the
// widget state machine twice per frame (double-firing every click), and the
// available probes cannot distinguish "absent" from "registered as id 0"
// (#2540). An explicit precondition beats an unsound guess. A creation that
// already builds widgets registers `System<IRSystem::SETTINGS_MENU>::create()`
// on its own instead of splicing this list.
inline std::list<IRSystem::SystemId>
inputSystems(const IRSystem::System<IRSystem::SETTINGS_MENU>::Params &params = {}) {
    return {
        IRSystem::createSystem<IRSystem::HITBOX_MOUSE_TEST_GUI>(),
        IRSystem::createSystem<IRSystem::WIDGET_INPUT>(),
        IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
        IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
        IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
        IRSystem::System<IRSystem::SETTINGS_MENU>::create(params),
    };
}

// Splice-ready RENDER-pipeline systems for the menu's widgets.
//
// **Precondition: `TEXT_TO_TRIXEL` must already be in this creation's RENDER
// pipeline, ahead of where this list is spliced** — it clears the GUI canvas
// and owns the shared text GPU resources every widget renderer's label path
// reuses. Same contract, and same rationale for not probing, as
// `HelpOverlay::systems()`.
//
// Dropdown renders last so its expanded item panel paints over the rows below
// it — painter order is the only depth the GUI canvas has.
inline std::list<IRSystem::SystemId> renderSystems() {
    return {
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_PANEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_LABEL>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_BUTTON>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_CHECKBOX>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_SLIDER>(),
        IRSystem::createSystem<IRSystem::WIDGET_RENDER_DROPDOWN>(),
    };
}

// Binds the menu toggle to @p button (Escape by default). Registers through
// the named path, so the binding shows up in the help overlay alongside
// everything else.
inline IRCommand::CommandId registerToggleCommand(int button = kDefaultToggleButton) {
    return IRCommand::createCommand<IRCommand::TOGGLE_SETTINGS_MENU>(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        button
    );
}

// --- Headless-test introspection -------------------------------------------
//
// `isOpen()` (from the component header) answers "did the toggle fire". These
// answer "did the menu actually materialize", which a flag alone cannot prove.
// Both resolve the running system through the `SystemName` registry (#2526)
// and read a clean negative when it is not registered.

inline const IRSystem::System<IRSystem::SETTINGS_MENU> *systemOrNull() {
    const IRSystem::SystemId id = IRSystem::findSystem(IRSystem::SETTINGS_MENU);
    if (id == IREntity::kNullEntity) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(id);
}

/// Number of setting rows currently spawned; 0 whenever the menu is closed.
inline int liveRowCount() {
    const auto *system = systemOrNull();
    return system == nullptr ? 0 : static_cast<int>(system->rows_.size());
}

/// The widget backing setting @p index while the menu is open, else
/// `kNullEntity` — the handle a GUI test clicks and asserts against.
inline IREntity::EntityId rowWidget(int index) {
    const auto *system = systemOrNull();
    if (system == nullptr || index < 0 || index >= static_cast<int>(system->rows_.size())) {
        return IREntity::kNullEntity;
    }
    return system->rows_[static_cast<std::size_t>(index)].control_;
}

/// The menu's QUIT button while the menu is open, else `kNullEntity`.
inline IREntity::EntityId quitButton() {
    const auto *system = systemOrNull();
    return system == nullptr ? IREntity::kNullEntity : system->quitButton_;
}

namespace detail {

/// Screen pixel at the center of @p widget, or `ivec2(0)` for `kNullEntity`.
inline IRMath::ivec2 widgetCenterScreenPx(IREntity::EntityId widget) {
    if (widget == IREntity::kNullEntity) {
        return IRMath::ivec2(0);
    }
    const IRMath::ivec2 pos = IREntity::getComponent<IRComponents::C_GuiPosition>(widget).pos_;
    const IRMath::ivec2 size = IREntity::getComponent<IRComponents::C_Widget>(widget).size_;
    return IRRender::guiTrixelToScreenPx(IRMath::vec2(pos) + IRMath::vec2(size) * 0.5f);
}

} // namespace detail

/// Screen pixel at the center of setting @p index's control, for a headless
/// GUI test's synthetic click; `ivec2(0)` while the menu is closed.
///
/// The menu centers itself on the GUI canvas, so a row's coordinate is not
/// known until it opens — a scripted shot table has to fill its MOVE target
/// from here at run time rather than hardcoding one.
inline IRMath::ivec2 rowWidgetScreenPx(int index) {
    return detail::widgetCenterScreenPx(rowWidget(index));
}

/// Screen pixel at the center of the QUIT button; `ivec2(0)` while closed.
inline IRMath::ivec2 quitButtonScreenPx() {
    return detail::widgetCenterScreenPx(quitButton());
}

/// Screen pixel at the center of item @p itemIndex in setting @p index's
/// **expanded** dropdown; `ivec2(0)` unless that row is an open dropdown whose
/// item list covers @p itemIndex.
///
/// An ENUM row takes two clicks — one on the header to expand, one on the item
/// — and the item strip only exists between them (`WIDGET_APPLY_DROPDOWN` grows
/// the hitbox to `size.y + itemHeight * n` while open). Mirrors that system's
/// row geometry so a scripted shot aims at the row it means to select.
inline IRMath::ivec2 enumItemScreenPx(int index, int itemIndex) {
    const auto *system = systemOrNull();
    if (system == nullptr || index < 0 || index >= static_cast<int>(system->rows_.size()) ||
        system->rows_[static_cast<std::size_t>(index)].kind_ !=
            IRComponents::SettingEntry::Kind::ENUM) {
        return IRMath::ivec2(0);
    }
    const IREntity::EntityId widget = system->rows_[static_cast<std::size_t>(index)].control_;
    if (widget == IREntity::kNullEntity) {
        return IRMath::ivec2(0);
    }
    const auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(widget);
    if (!dropdown.isOpen_ || itemIndex < 0 ||
        itemIndex >= static_cast<int>(dropdown.items_.size())) {
        return IRMath::ivec2(0);
    }
    const IRMath::ivec2 pos = IREntity::getComponent<IRComponents::C_GuiPosition>(widget).pos_;
    const IRMath::ivec2 size = IREntity::getComponent<IRComponents::C_Widget>(widget).size_;
    const float itemHeight = static_cast<float>(IRMath::max(1, dropdown.itemHeight_));
    return IRRender::guiTrixelToScreenPx(
        IRMath::vec2(
            static_cast<float>(pos.x) + static_cast<float>(size.x) * 0.5f,
            static_cast<float>(pos.y + size.y) + (static_cast<float>(itemIndex) + 0.5f) * itemHeight
        )
    );
}

} // namespace IRPrefab::SettingsMenu

#endif /* IR_PREFAB_SETTINGS_MENU_H */
