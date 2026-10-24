#ifndef WIDGET_TEXT_LAYOUT_H
#define WIDGET_TEXT_LAYOUT_H

// Where every widget kind, and every free-text entity, places its text. Each
// WIDGET_RENDER_* system and TEXT_TO_TRIXEL queue their glyphs through these
// helpers, and the GUI layout audit (gui_layout_audit.hpp) measures the
// commands the same helpers queue: a placement formula has exactly one copy,
// so the audit reads the text a renderer draws.

#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_widget_theme.hpp>
#include <irreden/render/gui_rect.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/widget_draw.hpp>

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IRPrefab::WidgetText {

// Everything `GuiText::expandTextToCommands` reads but the colour. `text_`
// views a string the caller owns for as long as the run is queued.
struct TextRun {
    std::string_view text_;
    IRMath::ivec2 pos_ = IRMath::ivec2(0);
    IRComponents::TextAlignH alignH_ = IRComponents::TextAlignH::LEFT;
    IRComponents::TextAlignV alignV_ = IRComponents::TextAlignV::TOP;
    // Alignment box; 0 on an axis aligns against the canvas extent.
    IRMath::ivec2 box_ = IRMath::ivec2(0);
    int wrapWidth_ = 0;
    int fontSize_ = IRPrefab::Widget::detail::kWidgetTextFontSize;
    IRRender::TextSize size_ = IRRender::TextSize::LARGE;
};

inline void queue(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    const TextRun &run,
    IRMath::ivec2 canvasSize,
    IRMath::Color color
) {
    IRPrefab::GuiText::queueGuiText(
        commands,
        run.text_,
        run.pos_,
        canvasSize,
        color,
        run.fontSize_,
        run.alignH_,
        run.alignV_,
        run.box_.x,
        run.box_.y,
        run.wrapWidth_,
        run.size_
    );
}

// Centred both ways inside `[pos, pos + size)`.
inline TextRun centered(
    std::string_view text, IRMath::ivec2 pos, IRMath::ivec2 size, IRRender::TextSize textSize
) {
    TextRun run;
    run.text_ = text;
    run.pos_ = pos;
    run.alignH_ = IRComponents::TextAlignH::CENTER;
    run.alignV_ = IRComponents::TextAlignV::CENTER;
    run.box_ = size;
    run.size_ = textSize;
    return run;
}

// Left-aligned at `pos`, centred vertically within `height`.
inline TextRun
leftCentered(std::string_view text, IRMath::ivec2 pos, int height, IRRender::TextSize textSize) {
    TextRun run;
    run.text_ = text;
    run.pos_ = pos;
    run.alignV_ = IRComponents::TextAlignV::CENTER;
    run.box_ = IRMath::ivec2(0, height);
    run.size_ = textSize;
    return run;
}

inline TextRun buttonCaption(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetButton &button,
    IRMath::ivec2 pos
) {
    return centered(button.label_, pos, widget.size_, button.textSize_);
}

inline TextRun checkboxCaption(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetCheckbox &checkbox,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    return leftCentered(
        checkbox.label_,
        IRMath::ivec2(pos.x + theme.checkboxBoxSize_ + theme.padding_ * 2, pos.y),
        widget.size_.y,
        checkbox.textSize_
    );
}

inline TextRun radioCaption(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetRadio &radio,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    return leftCentered(
        radio.label_,
        IRMath::ivec2(pos.x + theme.radioBoxSize_ + theme.padding_ * 2, pos.y),
        widget.size_.y,
        IRRender::TextSize::LARGE
    );
}

// A slider's label row: one line of its text size, or 0 with no label. The
// label and the value readout share it; the track and thumb sit below it.
inline int sliderLabelHeight(const IRComponents::C_WidgetSlider &slider) {
    return slider.label_.empty() ? 0 : IRRender::glyphMetrics(slider.textSize_).stepY_;
}

inline TextRun sliderLabel(const IRComponents::C_WidgetSlider &slider, IRMath::ivec2 pos) {
    return leftCentered(slider.label_, pos, sliderLabelHeight(slider), slider.textSize_);
}

// The value readout's text, in a buffer the caller keeps alive while the run
// is queued, so the renderer formats it without allocating.
struct SliderValueText {
    char text_[16];
};

inline SliderValueText sliderValueText(const IRComponents::C_WidgetSlider &slider) {
    SliderValueText value;
    std::snprintf(value.text_, sizeof(value.text_), "%.2f", slider.currentValue_);
    return value;
}

// Right-aligned within the label row; drawn only when the slider has a label.
inline TextRun sliderValue(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetSlider &slider,
    IRMath::ivec2 pos,
    const SliderValueText &value
) {
    TextRun run;
    run.text_ = value.text_;
    run.pos_ = pos;
    run.alignH_ = IRComponents::TextAlignH::RIGHT;
    run.alignV_ = IRComponents::TextAlignV::CENTER;
    run.box_ = IRMath::ivec2(widget.size_.x, sliderLabelHeight(slider));
    run.size_ = slider.textSize_;
    return run;
}

inline int panelTitleBarHeight(const IRPrefab::Widget::WidgetTheme &theme) {
    return IRRender::kGlyphStepY + theme.padding_ * 2;
}

inline TextRun panelTitle(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetPanel &panel,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    return centered(
        panel.title_,
        pos,
        IRMath::ivec2(widget.size_.x, panelTitleBarHeight(theme)),
        IRRender::TextSize::LARGE
    );
}

// The number field's text: the edit buffer while editing, else the formatted
// value, which the caller formats into a string it owns.
inline std::string_view
numberFieldShownText(const IRComponents::C_WidgetNumberField &field, const std::string &formatted) {
    return field.editing_ ? std::string_view(field.editBuffer_) : std::string_view(formatted);
}

inline int numberFieldCursorWidth(
    const IRComponents::C_WidgetNumberField &field, const IRPrefab::Widget::WidgetTheme &theme
) {
    return field.editing_ ? theme.textInputCursorWidth_ : 0;
}

// Right-aligned, clear of the edit cursor's reserve at the right edge.
inline TextRun numberFieldText(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetNumberField &field,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme,
    std::string_view shownText
) {
    const int padding = theme.padding_ * 2;
    TextRun run;
    run.text_ = shownText;
    run.pos_ = IRMath::ivec2(pos.x + padding, pos.y);
    run.alignH_ = IRComponents::TextAlignH::RIGHT;
    run.alignV_ = IRComponents::TextAlignV::CENTER;
    run.box_ = IRMath::ivec2(
        IRMath::max(0, widget.size_.x - padding * 2 - numberFieldCursorWidth(field, theme)),
        widget.size_.y
    );
    run.size_ = field.textSize_;
    return run;
}

inline TextRun textInputText(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetTextInput &textInput,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    return leftCentered(
        textInput.text_,
        IRMath::ivec2(pos.x + theme.padding_ * 2, pos.y),
        widget.size_.y,
        IRRender::TextSize::LARGE
    );
}

// Item `index` drawn on visible row `row` (0 = the top row).
inline TextRun listRow(
    const IRComponents::C_WidgetList &list,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme,
    int row,
    int index
) {
    const int itemH = list.rowHeight();
    return leftCentered(
        list.items_[static_cast<std::size_t>(index)],
        IRMath::ivec2(pos.x + theme.padding_ * 2, pos.y + row * itemH),
        itemH,
        list.textSize_
    );
}

// The selected item, or a dash with nothing selected.
inline std::string_view dropdownHeaderLabel(const IRComponents::C_WidgetDropdown &dd) {
    const int n = static_cast<int>(dd.items_.size());
    if (dd.selectedIndex_ >= 0 && dd.selectedIndex_ < n) {
        return dd.items_[static_cast<std::size_t>(dd.selectedIndex_)];
    }
    return "---";
}

inline TextRun dropdownHeader(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetDropdown &dd,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    return leftCentered(
        dropdownHeaderLabel(dd),
        IRMath::ivec2(
            pos.x + IRPrefab::Widget::detail::dropdownHorizontalInset(theme.padding_),
            pos.y
        ),
        widget.size_.y,
        dd.textSize_
    );
}

// The header's text region: the header less the chevron's reserve at its
// right edge, the reserve `dropdownMinimumWidth` sizes a header for.
inline GuiRect dropdownHeaderTextRegion(
    const IRComponents::C_Widget &widget,
    IRMath::ivec2 pos,
    const IRPrefab::Widget::WidgetTheme &theme
) {
    const int reserve = IRPrefab::Widget::detail::kDropdownTextChevronGap +
                        IRPrefab::Widget::detail::kDropdownChevronWidth +
                        IRPrefab::Widget::detail::dropdownHorizontalInset(theme.padding_);
    return {pos, IRMath::ivec2(IRMath::max(0, widget.size_.x - reserve), widget.size_.y)};
}

// The open item strip where @p layout places it: the visible window only.
inline GuiRect dropdownStripRect(
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetDropdown &dd,
    IRMath::ivec2 pos,
    const IRComponents::C_WidgetDropdown::StripLayout &layout
) {
    return {
        IRMath::ivec2(pos.x, pos.y + layout.offsetY_),
        IRMath::ivec2(widget.size_.x, dd.stripHeight(layout))
    };
}

inline TextRun dropdownStripRow(
    const IRComponents::C_WidgetDropdown &dd,
    IRMath::ivec2 pos,
    const IRComponents::C_WidgetDropdown::StripLayout &layout,
    const IRPrefab::Widget::WidgetTheme &theme,
    int index
) {
    return leftCentered(
        dd.items_[static_cast<std::size_t>(index)],
        IRMath::ivec2(
            pos.x + IRPrefab::Widget::detail::dropdownHorizontalInset(theme.padding_),
            pos.y + dd.itemTopOffsetY(layout, index)
        ),
        dd.rowHeight(),
        dd.textSize_
    );
}

inline TextRun label(const IRComponents::C_WidgetLabel &labelData, IRMath::ivec2 pos) {
    TextRun run;
    run.text_ = labelData.text_;
    run.pos_ = pos;
    run.size_ = labelData.textSize_;
    return run;
}

// The buffers a voxel-text style lays out into, kept by the caller across
// calls so a frame's layout reuses their capacity.
struct StyledTextScratch {
    IRRender::VoxelTextLayout voxelLayout_;
    IRRender::VoxelTextScratch voxelScratch_;
};

// One free-text entity (C_TextSegment + C_TextStyle), as TEXT_TO_TRIXEL draws
// it: isometric voxel letters for a voxel style, glyphs otherwise.
inline void queueStyledText(
    std::vector<IRRender::GlyphDrawCommand> &commands,
    std::string_view text,
    IRMath::ivec2 pos,
    IRMath::ivec2 canvasSize,
    const IRComponents::C_TextStyle &style,
    StyledTextScratch &scratch
) {
    if (IRRender::isVoxelTextStyle(style.voxel_)) {
        IRRender::layoutVoxelText(text, style.voxel_, scratch.voxelLayout_, scratch.voxelScratch_);
        IRPrefab::GuiText::expandVoxelTextToCommands(
            commands,
            scratch.voxelLayout_,
            pos,
            canvasSize,
            style.faceColors_,
            style.alignH_,
            style.alignV_,
            style.boxWidth_,
            style.boxHeight_
        );
        return;
    }
    IRPrefab::GuiText::expandTextToCommands(
        commands,
        text,
        pos,
        canvasSize,
        style.color_,
        style.wrapWidth_,
        style.alignH_,
        style.alignV_,
        style.boxWidth_,
        style.boxHeight_,
        style.fontSize_,
        style.textSize_
    );
}

// The union of the glyph cells `commands` draw, in GUI-canvas trixels: a
// command's cell is its styleFlags cell scaled by its font size. A glyph whose
// font rows are all unlit (a space) draws nothing and adds nothing. Empty
// when no command draws. `fontBuffer` is `GuiText::buildFontBuffer()`.
inline GuiRect drawnCellBounds(
    std::span<const IRRender::GlyphDrawCommand> commands, std::span<const std::uint32_t> fontBuffer
) {
    GuiRect bounds{};
    for (const IRRender::GlyphDrawCommand &cmd : commands) {
        const int fontSize = static_cast<int>(cmd.styleFlags & 0xFFu);
        const int cellW = static_cast<int>(
            (cmd.styleFlags >> IRRender::kGlyphStyleCellWidthShift) & IRRender::kGlyphStyleCellMask
        );
        const int cellH = static_cast<int>(
            (cmd.styleFlags >> IRRender::kGlyphStyleCellHeightShift) & IRRender::kGlyphStyleCellMask
        );
        bool lit = false;
        for (int row = 0; row < cellH && !lit; ++row) {
            const std::size_t index = static_cast<std::size_t>(cmd.firstFontRow) + row;
            lit = index < fontBuffer.size() && fontBuffer[index] != 0u;
        }
        if (!lit) {
            continue;
        }
        const IRMath::ivec2 pos(
            static_cast<std::int16_t>(cmd.positionPacked & 0xFFFFu),
            static_cast<std::int16_t>(cmd.positionPacked >> 16)
        );
        bounds = boundingBox(bounds, {pos, IRMath::ivec2(cellW, cellH) * fontSize});
    }
    return bounds;
}

} // namespace IRPrefab::WidgetText

#endif /* WIDGET_TEXT_LAYOUT_H */
