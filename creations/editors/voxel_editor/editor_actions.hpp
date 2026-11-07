#ifndef IR_VOXEL_EDITOR_EDITOR_ACTIONS_H
#define IR_VOXEL_EDITOR_EDITOR_ACTIONS_H

#include <irreden/ir_math.hpp>
#include <irreden/render/actions_window_layout.hpp>
#include <irreden/render/layout.hpp>

#include <array>
#include <string>
#include <string_view>

// The editor's searchable actions. Every key command registered in
// initCommands() is adopted into the catalog under `editor.<name>`; the
// controls below register their own actions so a click, a chord and a script
// run the same operation. Ids are stable keymap keys — rewording a label never
// orphans a user's binding, renaming an id does.

namespace IRVoxelEditor {

struct EditorActionRow {
    const char *id_;
    const char *label_;
    const char *section_;
};

// One row per sculpt toolbar button, in button order.
inline constexpr std::array<EditorActionRow, 12> kSculptToolbarActions{{
    {"editor.tool.add", "BRUSH TOOL", "TOOLS"},
    {"editor.tool.erase", "ERASE TOOL", "TOOLS"},
    {"editor.tool.paint", "RECOLOR TOOL", "TOOLS"},
    {"editor.tool.box", "BOX TOOL", "TOOLS"},
    {"editor.tool.line", "LINE TOOL", "TOOLS"},
    {"editor.part.new_voxel", "NEW VOXEL PART", "PARTS"},
    {"editor.toolbar.undo", "TOOLBAR UNDO", "HISTORY"},
    {"editor.toolbar.redo", "TOOLBAR REDO", "HISTORY"},
    {"editor.view.grid", "TOGGLE GRID", "VIEW"},
    {"editor.tool.bucket", "BUCKET TOOL", "TOOLS"},
    {"editor.tool.picker", "COLOR PICKER TOOL", "TOOLS"},
    {"editor.tool.shapes", "SHAPES TOOL", "TOOLS"},
}};

// One row per workspace tab, in `Workspace` order.
inline constexpr std::array<EditorActionRow, 5> kWorkspaceActions{{
    {"editor.workspace.sculpt", "SCULPT WORKSPACE", "WORKSPACES"},
    {"editor.workspace.assemble", "ASSEMBLE WORKSPACE", "WORKSPACES"},
    {"editor.workspace.generate", "GENERATE WORKSPACE", "WORKSPACES"},
    {"editor.workspace.animate", "ANIMATE WORKSPACE", "WORKSPACES"},
    {"editor.workspace.all", "ALL WORKSPACES", "WORKSPACES"},
}};

inline constexpr EditorActionRow kLocalEditAction{"editor.part.local_edit", "LOCAL EDIT", "PARTS"};
inline constexpr EditorActionRow kFocusAction{"editor.view.focus", "FOCUS WORK AREA", "VIEW"};

// Grid extent fields, indexed x / y / z: focus or step by one cell.
inline constexpr std::array<EditorActionRow, 3> kPartSizeFocusActions{{
    {"editor.part_size.width.focus", "FOCUS PART WIDTH", "PART SIZE"},
    {"editor.part_size.height.focus", "FOCUS PART HEIGHT", "PART SIZE"},
    {"editor.part_size.depth.focus", "FOCUS PART DEPTH", "PART SIZE"},
}};
inline constexpr std::array<EditorActionRow, 3> kPartSizeIncreaseActions{{
    {"editor.part_size.width.increase", "PART WIDTH +1", "PART SIZE"},
    {"editor.part_size.height.increase", "PART HEIGHT +1", "PART SIZE"},
    {"editor.part_size.depth.increase", "PART DEPTH +1", "PART SIZE"},
}};
inline constexpr std::array<EditorActionRow, 3> kPartSizeDecreaseActions{{
    {"editor.part_size.width.decrease", "PART WIDTH -1", "PART SIZE"},
    {"editor.part_size.height.decrease", "PART HEIGHT -1", "PART SIZE"},
    {"editor.part_size.depth.decrease", "PART DEPTH -1", "PART SIZE"},
}};

inline IRMath::ivec2 actionsWindowOrigin() {
    return IRPrefab::ActionsWindow::centeredOrigin(IRPrefab::Layout::guiCanvasSize());
}

// Section for a key command adopted from initCommands(), by its display name.
inline std::string adoptedCommandSection(std::string_view name) {
    struct Prefix {
        std::string_view name_;
        const char *section_;
    };
    static constexpr Prefix kSections[] = {
        {"UNDO", "HISTORY"},
        {"REDO", "HISTORY"},
        {"ZOOM", "CAMERA"},
        {"YAW", "CAMERA"},
        {"RESET CAMERA", "CAMERA"},
        {"SCREENSHOT", "CAPTURE"},
        {"RECORD", "CAPTURE"},
        {"MIRROR", "SYMMETRY"},
        {"RADIAL", "SYMMETRY"},
        {"LINEAR", "SYMMETRY"},
        {"ROTATIONAL", "SYMMETRY"},
        {"ERASE MODE", "MODES"},
        {"CLASS PAINT", "MODES"},
        {"TRANSFORM", "MODES"},
        {"PREV FRAME", "ANIMATION"},
        {"NEXT FRAME", "ANIMATION"},
        {"PLAY", "ANIMATION"},
        {"ADD FRAME", "ANIMATION"},
        {"DUPLICATE FRAME", "ANIMATION"},
        {"DELETE FRAME", "ANIMATION"},
        {"LOOP", "ANIMATION"},
        {"LOFT", "LOFT"},
        {"STAMP LOFT", "LOFT"},
        {"CLEAR LOFT", "LOFT"},
        {"ADD LAYER", "LAYERS"},
        {"PREV LAYER", "LAYERS"},
        {"NEXT LAYER", "LAYERS"},
        {"TOGGLE LAYER", "LAYERS"},
        {"JOINT", "RIG"},
        {"ADD JOINT", "RIG"},
        {"NEW CHAIN", "RIG"},
        {"BONE", "RIG"},
        {"SET BIND", "RIG"},
        {"ADD PART", "PARTS"},
        {"ADD SHAPE", "PARTS"},
        {"CYCLE PART", "PARTS"},
        {"TOGGLE MODE", "PARTS"},
        {"REMOVE PART", "PARTS"},
        {"NEXT PART", "PARTS"},
        {"PREVIOUS PART", "PARTS"},
        {"FINER", "LOD"},
        {"COARSER", "LOD"},
        {"CLEAR", "SCENE"},
        {"SAVE", "FILES"},
        {"LOAD", "FILES"},
        {"CANCEL", "GENERAL"},
        {"TOGGLE HELP", "GENERAL"},
        {"TOGGLE PERF", "GENERAL"},
    };
    for (const Prefix &prefix : kSections) {
        if (name.starts_with(prefix.name_)) {
            return prefix.section_;
        }
    }
    return "EDITOR";
}

namespace Session {

// Scripted-session checks on the action catalog and the Actions window.
enum class ActionCheckKind {
    // The action's chords as `chordToken`s joined by spaces equal `text_`.
    KEYS,
    // The editor ran the action `expected_` times since launch.
    RUNS,
    // The widget bound to the action shows `text_` as its badge.
    BADGE,
    // The window lists exactly the ids in `text_`, comma-joined, in order.
    LISTED,
    // The window's row for the action contains `text_`.
    ROW_CONTAINS,
    // The window's status line contains `text_`.
    STATUS,
    // The window is open (`expected_` 1) or closed (0).
    WINDOW_OPEN,
    // Discoverability audit: every interactive control in the toolbar, workspace
    // bar and PART SIZE panel is bound, grouped by section, and the groups cover
    // a tool pick, a button, a field focus and a value step.
    AUDIT,
};

struct ActionCheck {
    ActionCheckKind kind_ = ActionCheckKind::KEYS;
    std::string actionId_;
    std::string text_;
    int expected_ = 0;
    std::string name_;
};

bool evaluateActionCheck(const void *context, std::string &actual);

} // namespace Session

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_EDITOR_ACTIONS_H */
