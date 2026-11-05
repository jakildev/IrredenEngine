#ifndef IR_VOXEL_EDITOR_ACTIONS_SESSIONS_H
#define IR_VOXEL_EDITOR_ACTIONS_SESSIONS_H

#include <irreden/input/ir_input_types.hpp>
#include <irreden/render/actions_window_layout.hpp>

#include "editor_actions.hpp"
#include "session_builder.hpp"

#include <string_view>

// The Actions window, scripted. `actions_workflow` searches by name and by
// key, runs an unbound action, assigns ERASE TOOL a key already held by CLASS
// PAINT, replaces it, and proves the key fires the tool once with a matching
// badge. `actions_restart` is the relaunch against the same preferences
// directory: the binding, its dispatch and its badge survive. Run both under
// one `IR_PREFS_DIR`; without it a scripted run never touches the keymap and
// the restart has nothing to restore.

namespace IRVoxelEditor::Session {

namespace detail {

inline IRMath::vec2 actionsRectCenter(const IRPrefab::GuiRect &rect) {
    return IRMath::vec2(IRPrefab::ActionsWindow::rectCenter(rect));
}

// Clears the search field (Delete and Backspace both, wherever the click left
// the cursor) and types @p query; typeText's Enter drops the field's focus.
inline void replaceActionSearch(
    Builder &builder, const IRPrefab::ActionsWindow::Layout &layout, std::string_view query
) {
    constexpr int kClearKeys = 12;
    builder.clickGui(actionsRectCenter(layout.search_));
    for (int i = 0; i < kClearKeys; ++i) {
        builder.tapKey(IRInput::kKeyButtonDelete);
        builder.tapKey(IRInput::kKeyButtonBackspace);
    }
    builder.typeText(actionsRectCenter(layout.search_), query);
}

inline void openActionsWindow(Builder &builder) {
    builder.chordKey(IRInput::kKeyButtonLeftShift, IRInput::kKeyButtonSlash);
}

inline constexpr const char *kEraseToolId = "editor.tool.erase";
inline constexpr const char *kClassPaintId = "editor.class_paint";
inline constexpr const char *kGridId = "editor.view.grid";

} // namespace detail

inline Recipe buildActionsWorkflow(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    using IRPrefab::ActionsWindow::Button;
    Builder builder("actions_workflow", sceneSize, sceneOrigin);
    const IRPrefab::ActionsWindow::Layout layout =
        IRPrefab::ActionsWindow::layoutAt(kActionsWindowOrigin);
    const auto clickButton = [&](Button which) {
        builder.clickGui(detail::actionsRectCenter(layout.button(which)));
    };

    builder.segment("open_actions");
    builder.expectAction(ActionCheckKind::AUDIT, "", "", 0, "controls_grouped_by_action");
    detail::openActionsWindow(builder);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 1, "shift_slash_opens_actions");

    builder.segment("search_by_name");
    detail::replaceActionSearch(builder, layout, "toggle grid");
    builder.expectAction(ActionCheckKind::LISTED, "", detail::kGridId, 0, "name_search_finds_grid");

    builder.segment("search_by_key");
    detail::replaceActionSearch(builder, layout, "f1");
    builder.expectAction(
        ActionCheckKind::LISTED,
        "",
        "editor.toggle_help",
        0,
        "key_search_finds_help_by_f1"
    );

    builder.segment("run_unbound");
    detail::replaceActionSearch(builder, layout, "toggle grid");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::RUN);
    builder.expectAction(ActionCheckKind::KEYS, detail::kGridId, "", 0, "grid_has_no_key");
    builder.expectAction(ActionCheckKind::RUNS, detail::kGridId, "", 1, "run_fires_unbound_once");
    builder.expectAction(ActionCheckKind::STATUS, "", "RAN TOGGLE GRID", 0, "run_reports");

    builder.segment("assign_conflict");
    detail::replaceActionSearch(builder, layout, "erase tool");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::ASSIGN);
    builder.tapKey(IRInput::kKeyButtonU);
    builder.expectAction(ActionCheckKind::STATUS, "", "U IS USED BY CLASS PAINT", 0, "conflict");
    builder.expectAction(ActionCheckKind::KEYS, detail::kEraseToolId, "", 0, "conflict_holds");
    builder.expectAction(ActionCheckKind::KEYS, detail::kClassPaintId, "U", 0, "owner_holds");

    builder.segment("replace_conflict");
    builder.clickGui(detail::actionsRectCenter(layout.replace_));
    builder.expectAction(ActionCheckKind::KEYS, detail::kEraseToolId, "U", 0, "replace_binds");
    builder.expectAction(ActionCheckKind::KEYS, detail::kClassPaintId, "", 0, "owner_released");
    builder.expectAction(ActionCheckKind::ROW_CONTAINS, detail::kEraseToolId, "= U", 0, "list_u");

    builder.segment("close_and_fire");
    clickButton(Button::CLOSE);
    builder.tapKey(IRInput::kKeyButtonU);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "close_closes");
    builder.expectAction(ActionCheckKind::RUNS, detail::kEraseToolId, "", 1, "u_fires_erase_once");

    builder.segment("badge");
    builder.holdKey(IRInput::kKeyButtonLeftAlt);
    builder
        .expectAction(ActionCheckKind::BADGE, detail::kEraseToolId, "U", 0, "badge_matches_list");

    builder.segment("badge_released");
    builder.releaseKey(IRInput::kKeyButtonLeftAlt);
    return builder.finish();
}

inline Recipe buildActionsRestart(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    Builder builder("actions_restart", sceneSize, sceneOrigin);
    const IRPrefab::ActionsWindow::Layout layout =
        IRPrefab::ActionsWindow::layoutAt(kActionsWindowOrigin);

    builder.segment("restored");
    builder.holdKey(IRInput::kKeyButtonLeftAlt);
    builder.expectAction(ActionCheckKind::KEYS, detail::kEraseToolId, "U", 0, "erase_keeps_u");
    builder
        .expectAction(ActionCheckKind::KEYS, detail::kClassPaintId, "", 0, "class_paint_unbound");
    builder.expectAction(ActionCheckKind::BADGE, detail::kEraseToolId, "U", 0, "badge_restored");

    builder.segment("fire_after_restart");
    builder.releaseKey(IRInput::kKeyButtonLeftAlt);
    builder.tapKey(IRInput::kKeyButtonU);
    builder.expectAction(ActionCheckKind::RUNS, detail::kEraseToolId, "", 1, "u_fires_erase_once");

    builder.segment("list_after_restart");
    detail::openActionsWindow(builder);
    detail::replaceActionSearch(builder, layout, "erase tool");
    builder.holdKey(IRInput::kKeyButtonLeftAlt);
    builder.expectAction(ActionCheckKind::ROW_CONTAINS, detail::kEraseToolId, "= U", 0, "list_u");
    builder.expectAction(ActionCheckKind::BADGE, detail::kEraseToolId, "U", 0, "badge_agrees");

    builder.segment("released");
    builder.releaseKey(IRInput::kKeyButtonLeftAlt);
    return builder.finish();
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_ACTIONS_SESSIONS_H */
