#ifndef WORKSPACE_CONTROLS_WORKFLOW_H
#define WORKSPACE_CONTROLS_WORKFLOW_H

#include "palette.hpp"
#include "part_size_panel.hpp"
#include "sculpt_panel.hpp"
#include "session_builder.hpp"

namespace IRVoxelEditor::Session {

enum class WorkspaceCheck {
    HELP,
    HELP_BLOCKS,
    CLOSE_HELP,
    CAPTURE_TEXT,
    PICKER_KEY,
    HINTS,
    ORBIT,
    RELEASE,
    MOVE_DOCK,
    MOVED_SWATCH,
    UI_ORBIT,
    UI_PAN,
    UI_MIDDLE,
    SCROLL_OVER_DOCK,
    RESTORE,
    SNAP,
    RESET,
    EXPOSED_FLOOR,
    SAVE_LAYOUT,
    RESTORED,
    RESTORED_SWATCH
};
constexpr std::array kWorkspaceChecks{
    WorkspaceCheck::HELP,           WorkspaceCheck::HELP_BLOCKS,
    WorkspaceCheck::CLOSE_HELP,     WorkspaceCheck::CAPTURE_TEXT,
    WorkspaceCheck::PICKER_KEY,     WorkspaceCheck::HINTS,
    WorkspaceCheck::ORBIT,          WorkspaceCheck::RELEASE,
    WorkspaceCheck::MOVE_DOCK,      WorkspaceCheck::MOVED_SWATCH,
    WorkspaceCheck::UI_ORBIT,       WorkspaceCheck::UI_PAN,
    WorkspaceCheck::UI_MIDDLE,      WorkspaceCheck::SCROLL_OVER_DOCK,
    WorkspaceCheck::RESTORE,        WorkspaceCheck::SNAP,
    WorkspaceCheck::RESET,          WorkspaceCheck::EXPOSED_FLOOR,
    WorkspaceCheck::SAVE_LAYOUT,    WorkspaceCheck::RESTORED,
    WorkspaceCheck::RESTORED_SWATCH
};
constexpr IRMath::ivec2 kMovedPalette{700, 320};
constexpr IRMath::vec2 movedSwatchAim() {
    return paletteSwatchCenterGuiTrixel(4) + IRMath::vec2(kMovedPalette - kPalettePanelPos);
}
constexpr float workspaceControlsYaw(int shot) {
    return (shot < 5 ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}
enum class WorkspaceAim { GRID_FIELD, PALETTE_TITLE, PALETTE_SWATCH, RESET };
constexpr IRMath::vec2 workspaceAim(WorkspaceAim aim) {
    return {-1.0f, static_cast<float>(aim)};
}
IRMath::vec2 resolveWorkspaceAim(IRMath::vec2 point);
bool evaluateWorkspaceControlCheck(const void *context, std::string &actual);

inline Recipe buildWorkspaceControls(bool restore = false) {
    using Check = WorkspaceCheck;
    using Type = IRVideo::GuiInputEvent::Type;
    constexpr std::array labels{
        "backtick_help",
        "help_blocks_underlying_input",
        "f1_closes_help",
        "field_captures_keys",
        "picker_shortcut",
        "alt_tool_hints",
        "alt_orbit",
        "alt_released",
        "move_palette",
        "moved_palette_swatch",
        "moved_palette_blocks_orbit",
        "moved_palette_blocks_space_pan",
        "moved_palette_blocks_middle_pan",
        "moved_palette_scroll",
        "restore_saved_layout",
        "right_snap_palette",
        "reset_layout",
        "exposed_floor_edit",
        "save_final_layout",
        "restart_restores_layout",
        "restart_swatch_works"
    };
    Recipe recipe;
    recipe.name_ = restore ? "workspace_restore" : "workspace_controls";
    for (std::size_t i = 0; i < kWorkspaceChecks.size(); ++i) {
        const bool restoreStep =
            kWorkspaceChecks[i] == Check::RESTORED || kWorkspaceChecks[i] == Check::RESTORED_SWATCH;
        if (restoreStep != restore)
            continue;
        Segment s;
        s.label_ = labels[i];
        s.zoom_ = 8.0f;
        int frame = 0;
        const auto event = [&](Type type, IRInput::KeyMouseButtons button = IRInput::kNullButton) {
            s.events_.push_back({.frameOffset_ = frame, .type_ = type, .button_ = button});
            frame += 2;
        };
        const auto move = [&](IRMath::vec2 point) {
            s.guiAims_.push_back({static_cast<int>(s.events_.size()), point});
            event(Type::MOVE);
        };
        const auto tap = [&](IRInput::KeyMouseButtons button) {
            event(Type::PRESS, button);
            event(Type::RELEASE, button);
        };
        const auto click = [&](IRMath::vec2 point) {
            move(point);
            tap(IRInput::kMouseButtonLeft);
        };
        switch (kWorkspaceChecks[i]) {
        case Check::HELP:
            tap(IRInput::kKeyButtonGraveAccent);
            break;
        case Check::HELP_BLOCKS:
            tap(IRInput::kKeyButton5);
            click({800, 650});
            event(Type::PRESS, IRInput::kKeyButtonLeftAlt);
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move({840, 650});
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            event(Type::RELEASE, IRInput::kKeyButtonLeftAlt);
            event(Type::PRESS, IRInput::kKeyButtonSpace);
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move({860, 650});
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            event(Type::RELEASE, IRInput::kKeyButtonSpace);
            event(Type::PRESS, IRInput::kMouseButtonMiddle);
            move({880, 650});
            event(Type::RELEASE, IRInput::kMouseButtonMiddle);
            break;
        case Check::CLOSE_HELP:
            tap(IRInput::kKeyButtonF1);
            break;
        case Check::CAPTURE_TEXT:
            click(workspaceAim(WorkspaceAim::GRID_FIELD));
            tap(IRInput::kKeyButton5);
            tap(IRInput::kKeyButtonGraveAccent);
            break;
        case Check::PICKER_KEY:
            tap(IRInput::kKeyButtonEscape);
            click(sculptButtonCenter(0));
            tap(IRInput::kKeyButton5);
            break;
        case Check::HINTS:
            click(sculptButtonCenter(0));
            move({800, 650});
            event(Type::PRESS, IRInput::kKeyButtonLeftAlt);
            break;
        case Check::ORBIT:
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move({840, 650});
            break;
        case Check::RELEASE:
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            event(Type::RELEASE, IRInput::kKeyButtonLeftAlt);
            break;
        case Check::SAVE_LAYOUT:
        case Check::MOVE_DOCK:
            move(workspaceAim(WorkspaceAim::PALETTE_TITLE));
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move(IRMath::vec2(kMovedPalette + IRMath::ivec2(30, 6)));
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            break;
        case Check::RESTORED_SWATCH:
        case Check::MOVED_SWATCH:
            click(workspaceAim(WorkspaceAim::PALETTE_SWATCH));
            break;
        case Check::UI_ORBIT:
            move(workspaceAim(WorkspaceAim::PALETTE_SWATCH));
            event(Type::PRESS, IRInput::kKeyButtonLeftAlt);
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move(movedSwatchAim() + IRMath::vec2(8, 0));
            break;
        case Check::UI_PAN:
        case Check::UI_MIDDLE:
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            event(Type::RELEASE, IRInput::kKeyButtonLeftAlt);
            move(workspaceAim(WorkspaceAim::PALETTE_SWATCH));
            if (kWorkspaceChecks[i] == Check::UI_PAN)
                event(Type::PRESS, IRInput::kKeyButtonSpace);
            event(
                Type::PRESS,
                kWorkspaceChecks[i] == Check::UI_PAN ? IRInput::kMouseButtonLeft
                                                     : IRInput::kMouseButtonMiddle
            );
            move(movedSwatchAim() + IRMath::vec2(8, 0));
            event(
                Type::RELEASE,
                kWorkspaceChecks[i] == Check::UI_PAN ? IRInput::kMouseButtonLeft
                                                     : IRInput::kMouseButtonMiddle
            );
            event(Type::RELEASE, IRInput::kKeyButtonSpace);
            break;
        case Check::SCROLL_OVER_DOCK:
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            event(Type::RELEASE, IRInput::kKeyButtonLeftAlt);
            move(workspaceAim(WorkspaceAim::PALETTE_SWATCH));
            event(Type::SCROLL);
            s.events_.back().scroll_ = {1.0f, 0.1f};
            break;
        case Check::RESTORED:
        case Check::RESTORE:
            break;
        case Check::SNAP:
            move(workspaceAim(WorkspaceAim::PALETTE_TITLE));
            event(Type::PRESS, IRInput::kMouseButtonLeft);
            move({1596, 350});
            event(Type::RELEASE, IRInput::kMouseButtonLeft);
            break;
        case Check::RESET:
            click(workspaceAim(WorkspaceAim::RESET));
            break;
        case Check::EXPOSED_FLOOR:
            click(movedSwatchAim());
            break;
        }
        s.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateWorkspaceControlCheck,
                &kWorkspaceChecks[i],
                labels[i]
            )
        );
        recipe.segments_.push_back(std::move(s));
    }
    return recipe;
}
} // namespace IRVoxelEditor::Session
#endif
