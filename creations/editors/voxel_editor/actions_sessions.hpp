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
        IRPrefab::ActionsWindow::layoutAt(IRMath::ivec2(0));
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
        "editor.screenshot,editor.toggle_help",
        0,
        "key_search_finds_f1_and_f12"
    );

    builder.segment("run_unbound");
    detail::replaceActionSearch(builder, layout, "toggle grid");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::RUN);
    builder.expectAction(ActionCheckKind::KEYS, detail::kGridId, "", 0, "grid_has_no_key");
    builder.expectAction(ActionCheckKind::RUNS, detail::kGridId, "", 1, "run_fires_unbound_once");
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "run_closes_actions");

    builder.segment("assign_conflict");
    detail::openActionsWindow(builder);
    detail::replaceActionSearch(builder, layout, "erase tool");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::ASSIGN);
    builder.tapKey(IRInput::kKeyButtonU);
    builder.expectAction(ActionCheckKind::STATUS, "", "U IS USED BY CLASS PAINT", 0, "conflict");
    builder.expectAction(ActionCheckKind::KEYS, detail::kEraseToolId, "2", 0, "conflict_holds");
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

    builder.segment("modal_input");
    detail::openActionsWindow(builder);
    builder.tapKey(IRInput::kKeyButton1);
    builder.expectAction(ActionCheckKind::RUNS, "editor.tool.add", "", 0, "modal_blocks_tool_key");
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 1, "modal_remains_open");

    builder.segment("run_shapes");
    detail::replaceActionSearch(builder, layout, "shapes tool");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::RUN);
    builder.expectAction(ActionCheckKind::RUNS, "editor.tool.shapes", "", 1, "run_shapes_once");
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "shapes_closes_modal");

    builder.segment("run_reset", 8.0f);
    detail::openActionsWindow(builder);
    detail::replaceActionSearch(builder, layout, "reset camera");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::RUN);
    builder.expectCameraZoom(4.0f, true, "run_resets_zoom");
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "reset_closes_modal");

    builder.segment("run_self_toggle");
    detail::openActionsWindow(builder);
    detail::replaceActionSearch(builder, layout, "actions window");
    builder.clickGui(IRMath::vec2(layout.rowCenter(0)));
    clickButton(Button::RUN);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "self_toggle_closes");

    builder.segment("reopen_after_self_toggle");
    detail::openActionsWindow(builder);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 1, "self_toggle_can_reopen");
    builder.segment("close_after_self_toggle");
    builder.tapKey(IRInput::kKeyButtonEscape);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "escape_closes");
    return builder.finish();
}

inline Recipe buildActionsRestart(IRMath::ivec3 sceneSize, IRMath::vec3 sceneOrigin) {
    Builder builder("actions_restart", sceneSize, sceneOrigin);
    const IRPrefab::ActionsWindow::Layout layout =
        IRPrefab::ActionsWindow::layoutAt(IRMath::ivec2(0));

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
    builder.segment("close_after_restart");
    builder.releaseKey(IRInput::kKeyButtonLeftAlt);
    detail::openActionsWindow(builder);
    builder.expectAction(ActionCheckKind::WINDOW_OPEN, "", "", 0, "toggle_closes_actions");
    builder.holdKey(IRInput::kKeyButtonLeftAlt);
    builder.expectAction(ActionCheckKind::BADGE, detail::kEraseToolId, "U", 0, "badge_agrees");

    builder.segment("released");
    builder.releaseKey(IRInput::kKeyButtonLeftAlt);
    return builder.finish();
}

} // namespace IRVoxelEditor::Session

#endif /* IR_VOXEL_EDITOR_ACTIONS_SESSIONS_H */
