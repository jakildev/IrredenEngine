#ifndef IR_PREFAB_ACTIONS_WINDOW_LAYOUT_H
#define IR_PREFAB_ACTIONS_WINDOW_LAYOUT_H

#include <irreden/ir_math.hpp>

#include <irreden/render/gui_rect.hpp>

#include <array>

namespace IRPrefab::ActionsWindow {

inline constexpr IRMath::ivec2 kSize{372, 236};
inline constexpr int kPad = 8;
inline constexpr int kFieldHeight = 18;
inline constexpr int kListTop = 50;
inline constexpr int kListHeight = 132;
inline constexpr int kListItemHeight = 12;
inline constexpr int kStatusTop = 188;
inline constexpr int kButtonTop = 210;
inline constexpr int kButtonHeight = 18;
inline constexpr int kButtonCount = 6;
inline constexpr int kButtonGap = 4;
inline constexpr int kPromptButtonWidth = 64;

enum class Button { RUN, ASSIGN, ADD, REMOVE, RESET, CLOSE };

/// Every rect of the window for a top-left corner at @p origin. A pure function
/// of the origin, so a scripted GUI session aims its clicks at the same rects
/// the window builds, before the window exists.
struct Layout {
    GuiRect panel_;
    GuiRect search_;
    GuiRect list_;
    GuiRect status_;
    GuiRect capture_;
    GuiRect replace_;
    GuiRect cancel_;
    std::array<GuiRect, kButtonCount> buttons_{};

    GuiRect button(Button which) const {
        return buttons_[static_cast<std::size_t>(which)];
    }

    /// Centre of the list's @p row-th visible row.
    IRMath::ivec2 rowCenter(int row) const {
        return IRMath::ivec2(
            list_.pos_.x + list_.size_.x / 2,
            list_.pos_.y + row * kListItemHeight + kListItemHeight / 2
        );
    }
};

inline IRMath::ivec2 rectCenter(const GuiRect &rect) {
    return rect.pos_ + rect.size_ / 2;
}

inline Layout layoutAt(IRMath::ivec2 origin) {
    Layout layout;
    const int innerWidth = kSize.x - 2 * kPad;
    layout.panel_ = {origin, kSize};
    layout.search_ = {origin + IRMath::ivec2(kPad, 26), IRMath::ivec2(innerWidth, kFieldHeight)};
    layout.list_ = {origin + IRMath::ivec2(kPad, kListTop), IRMath::ivec2(innerWidth, kListHeight)};
    layout.status_ = {
        origin + IRMath::ivec2(kPad, kStatusTop),
        IRMath::ivec2(innerWidth - 2 * (kPromptButtonWidth + kButtonGap), 16)
    };
    const int promptX = kSize.x - kPad - 2 * kPromptButtonWidth - kButtonGap;
    layout.replace_ = {
        origin + IRMath::ivec2(promptX, kStatusTop - 1),
        IRMath::ivec2(kPromptButtonWidth, 16)
    };
    layout.cancel_ = {
        origin + IRMath::ivec2(promptX + kPromptButtonWidth + kButtonGap, kStatusTop - 1),
        IRMath::ivec2(kPromptButtonWidth, 16)
    };
    layout.capture_ = {
        origin + IRMath::ivec2(promptX, kStatusTop - 1),
        IRMath::ivec2(2 * kPromptButtonWidth + kButtonGap, 16)
    };
    const int buttonWidth = (innerWidth - (kButtonCount - 1) * kButtonGap) / kButtonCount;
    for (int i = 0; i < kButtonCount; ++i) {
        layout.buttons_[static_cast<std::size_t>(i)] = {
            origin + IRMath::ivec2(kPad + i * (buttonWidth + kButtonGap), kButtonTop),
            IRMath::ivec2(buttonWidth, kButtonHeight)
        };
    }
    return layout;
}

/// The corner `layoutAt` centres the window on a GUI canvas of @p canvasSize.
inline IRMath::ivec2 centeredOrigin(IRMath::ivec2 canvasSize) {
    return IRMath::max((canvasSize - kSize) / 2, IRMath::ivec2(0));
}

} // namespace IRPrefab::ActionsWindow

#endif /* IR_PREFAB_ACTIONS_WINDOW_LAYOUT_H */
