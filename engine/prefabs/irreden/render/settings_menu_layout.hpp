#ifndef SETTINGS_MENU_LAYOUT_H
#define SETTINGS_MENU_LAYOUT_H

#include <irreden/ir_math.hpp>

#include <irreden/render/gui_rect.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/widget_draw.hpp>

#include <vector>

namespace IRPrefab::SettingsMenu {

inline constexpr int kWidth = 380;
inline constexpr int kMargin = 16;
inline constexpr int kPad = 12;
inline constexpr int kRowHeight = 24;
inline constexpr int kRowGap = 6;
inline constexpr int kTitleHeight = 26;
inline constexpr int kQuitHeight = 26;
inline constexpr int kEnumLabelPercent = 45;
inline constexpr int kEnumLabelControlGap = IRRender::kGlyphStepX;

struct LayoutRowInput {
    bool hasSeparateLabel_ = false;
    int labelTextWidth_ = 0;
    int controlMinWidth_ = 0;
};

struct LayoutInput {
    IRMath::ivec2 canvasSize_ = IRMath::ivec2(0);
    std::vector<LayoutRowInput> rows_;
    int hintTextWidth_ = 0;
};

struct RowLayout {
    GuiRect label_;
    IRMath::ivec2 labelOrigin_ = IRMath::ivec2(0);
    GuiRect control_;
};

struct MenuLayout {
    GuiRect panel_;
    std::vector<RowLayout> rows_;
    GuiRect hint_;
    IRMath::ivec2 hintOrigin_ = IRMath::ivec2(0);
    GuiRect quit_;
    bool enumRowsStacked_ = false;
};

namespace detail {

inline int textBaseline(int rowTop) {
    const int textHeight = IRRender::kGlyphHeight * Widget::detail::kWidgetTextFontSize;
    return rowTop + (kRowHeight - textHeight) / 2;
}

} // namespace detail

inline MenuLayout layoutMenu(const LayoutInput &input) {
    MenuLayout layout;
    const int panelWidth = IRMath::min(kWidth, input.canvasSize_.x - 2 * kMargin);
    const int contentWidth = panelWidth - 2 * kPad;
    const int labelColumnWidth = contentWidth * kEnumLabelPercent / 100;
    const int controlColumnWidth = contentWidth - labelColumnWidth;

    for (const LayoutRowInput &row : input.rows_) {
        if (!row.hasSeparateLabel_) {
            continue;
        }
        const int labelMinWidth = row.labelTextWidth_ + kEnumLabelControlGap +
                                  Widget::detail::kDropdownTextParityAllowance;
        if (labelMinWidth > labelColumnWidth || row.controlMinWidth_ > controlColumnWidth) {
            layout.enumRowsStacked_ = true;
            break;
        }
    }
    const int rowStride = kRowHeight + kRowGap;
    int rowsHeight = 0;
    for (const LayoutRowInput &row : input.rows_) {
        rowsHeight += rowStride;
        if (layout.enumRowsStacked_ && row.hasSeparateLabel_) {
            rowsHeight += kRowHeight;
        }
    }
    const int hintHeight = input.hintTextWidth_ > 0 ? rowStride : 0;
    const int panelHeight = 2 * kPad + kTitleHeight + rowsHeight + hintHeight + kQuitHeight;
    layout.panel_ = {
        IRMath::ivec2(
            IRMath::max(kMargin, (input.canvasSize_.x - panelWidth) / 2),
            IRMath::max(kMargin, (input.canvasSize_.y - panelHeight) / 2)
        ),
        IRMath::ivec2(panelWidth, panelHeight)
    };

    const int contentX = layout.panel_.pos_.x + kPad;
    int y = layout.panel_.pos_.y + kPad + kTitleHeight;
    layout.rows_.reserve(input.rows_.size());
    for (const LayoutRowInput &inputRow : input.rows_) {
        RowLayout row;
        if (!inputRow.hasSeparateLabel_) {
            row.control_ = {IRMath::ivec2(contentX, y), IRMath::ivec2(contentWidth, kRowHeight)};
            y += rowStride;
            layout.rows_.push_back(row);
            continue;
        }

        row.labelOrigin_ = IRMath::ivec2(contentX, detail::textBaseline(y));
        row.label_ = {
            IRRender::parityAlignedPosition(row.labelOrigin_, input.canvasSize_),
            IRMath::ivec2(
                inputRow.labelTextWidth_,
                GuiText::glyphHeight(Widget::detail::kWidgetTextFontSize)
            )
        };
        if (layout.enumRowsStacked_) {
            row.control_ = {
                IRMath::ivec2(contentX, y + kRowHeight),
                IRMath::ivec2(contentWidth, kRowHeight)
            };
            y += rowStride + kRowHeight;
        } else {
            row.control_ = {
                IRMath::ivec2(contentX + labelColumnWidth, y),
                IRMath::ivec2(controlColumnWidth, kRowHeight)
            };
            y += rowStride;
        }
        layout.rows_.push_back(row);
    }

    if (input.hintTextWidth_ > 0) {
        layout.hintOrigin_ = IRMath::ivec2(contentX, detail::textBaseline(y));
        layout.hint_ = {
            IRRender::parityAlignedPosition(layout.hintOrigin_, input.canvasSize_),
            IRMath::ivec2(
                input.hintTextWidth_,
                GuiText::glyphHeight(Widget::detail::kWidgetTextFontSize)
            )
        };
        y += rowStride;
    }
    layout.quit_ = {IRMath::ivec2(contentX, y), IRMath::ivec2(contentWidth, kQuitHeight)};
    return layout;
}

} // namespace IRPrefab::SettingsMenu

#endif /* SETTINGS_MENU_LAYOUT_H */
