#ifndef PALETTE_PANEL_H
#define PALETTE_PANEL_H

#include "editor_widget_geometry.hpp"
#include "palette_model.hpp"

#include <irreden/render/trixel_text.hpp>
#include <irreden/render/widgets.hpp>

#include <array>
#include <string>
#include <vector>

namespace IRVoxelEditor {

class PalettePanel {
  public:
    void create(IRMath::ivec2 position = kPalettePanelPos, IRMath::ivec2 size = kPalettePanelSize) {
        using namespace IRPrefab::Widget;
        IR_ASSERT(
            size.x >= 204 && size.y >= 440,
            "Palette panel requires at least 204 x 440 trixels"
        );
        m_position = position;
        m_size = size;
        m_panel = makePanel(position, size, "PALETTE");
        IREntity::setComponent(m_panel, IRComponents::C_HitBox2DGui{size});
        m_widgets.push_back(m_panel);
        m_selected = label({12, 22}, "");
        m_rgb = label({12, 38}, "");
        for (int index = 0; index < kPaletteCount; ++index) {
            const auto offset =
                kPaletteSwatchOffset +
                IRMath::ivec2(
                    index % kPaletteGridCols * (kPaletteSwatchSize + kPaletteSwatchGap),
                    index / kPaletteGridCols * (kPaletteSwatchSize + kPaletteSwatchGap)
                );
            m_swatches[index] = makeColorSwatch(
                position + offset,
                IRMath::ivec2(kPaletteSwatchSize),
                kPaletteColors[index],
                index == 0
            );
            m_widgets.push_back(m_swatches[index]);
        }
        m_previous = button({12, 236}, {36, 18}, "PREV");
        m_pageLabel = label({64, 240}, "1 / 1");
        m_next = button({156, 236}, {36, 18}, "NEXT");
        label({12, 262}, "HEX (#RRGGBB)");
        m_hex = makeTextInput(position + IRMath::ivec2(12, 278), {180, 18}, "", 7);
        m_widgets.push_back(m_hex);
        m_edit = button({12, 302}, {84, 18}, "EDIT");
        m_add = button({108, 302}, {84, 18}, "ADD");
        label({12, 326}, "GIMP PALETTE FILE");
        m_file = makeTextInput(
            position + IRMath::ivec2(12, 342),
            {180, 18},
            "data/palettes/custom.gpl",
            1024
        );
        m_widgets.push_back(m_file);
        m_load = button({12, 366}, {84, 18}, "LOAD");
        m_save = button({108, 366}, {84, 18}, "SAVE");
        m_defaults = button({12, 390}, {180, 18}, "RESTORE DEFAULTS");
        m_statusLabel = label({12, 416}, "16 default colors");
    }

    // Called after WIDGET_INPUT; returns true only when selection or palette
    // data changes. Palette choices are independent of document edit history.
    bool update(EditorPalette &palette, int &selected) {
        using namespace IRPrefab::Widget;
        selected = IRMath::clamp(selected, EditorPalette::kSampled, palette.size() - 1);
        bool changed = selected != m_lastSelected;
        if (changed && selected >= 0)
            m_page = selected / kPaletteCount;
        bool redraw =
            changed || palette.size() != m_lastSize || isVisible(m_panel) != m_lastVisible;
        for (int slot = 0; slot < kPaletteCount; ++slot) {
            const int index = m_page * kPaletteCount + slot;
            if (index < palette.size() && wasClicked(m_swatches[slot])) {
                selected = index;
                changed = true;
                redraw = true;
                m_status.clear();
            }
        }
        const int pages = (palette.size() + kPaletteCount - 1) / kPaletteCount;
        if (wasClicked(m_previous) && m_page > 0) {
            --m_page;
            redraw = true;
        }
        if (wasClicked(m_next) && m_page + 1 < pages) {
            ++m_page;
            redraw = true;
        }
        const bool edit = wasClicked(m_edit);
        const bool add = wasClicked(m_add);
        if (edit || add) {
            const auto color = parsePaletteHex(textInputValue(m_hex));
            if (!color) {
                m_status = "Use six hex digits";
            } else if (add && !palette.add(*color)) {
                m_status = "Palette is full (256)";
            } else {
                if (edit)
                    palette.edit(selected, *color);
                else
                    selected = palette.size() - 1;
                m_page = selected / kPaletteCount;
                m_status = edit ? "Swatch updated" : "Color added";
                changed = true;
                redraw = true;
            }
        }
        if (wasClicked(m_defaults)) {
            palette.resetDefaults();
            selected = 0;
            m_page = 0;
            m_status = "Defaults restored";
            changed = true;
            redraw = true;
        }
        if (wasClicked(m_load)) {
            const auto result = palette.loadGpl(textInputValue(m_file));
            m_status = result.ok_ ? "Palette loaded" : result.error_;
            if (result.ok_) {
                selected = 0;
                m_page = 0;
                changed = true;
                redraw = true;
            }
        }
        if (wasClicked(m_save)) {
            const auto result = palette.saveGpl(textInputValue(m_file));
            m_status = result.ok_ ? "Palette saved" : result.error_;
        }
        if (redraw)
            refresh(palette, selected);
        setLabelText(
            m_statusLabel,
            clipped(
                m_status.empty()
                    ? palette.name() + " / " + std::to_string(palette.size()) + " colors"
                    : m_status
            )
        );
        return changed;
    }

    void refresh(const EditorPalette &palette, int selected) {
        using namespace IRPrefab::Widget;
        selected = IRMath::clamp(selected, EditorPalette::kSampled, palette.size() - 1);
        const int pages = (palette.size() + kPaletteCount - 1) / kPaletteCount;
        m_page = IRMath::clamp(m_page, 0, pages - 1);
        const bool visible = isVisible(m_panel);
        for (int slot = 0; slot < kPaletteCount; ++slot) {
            const int index = m_page * kPaletteCount + slot;
            const bool present = index < palette.size();
            setVisible(m_swatches[slot], visible && present);
            setColorSwatchSelected(m_swatches[slot], present && index == selected);
            if (present)
                setColorSwatchColor(m_swatches[slot], palette.color(index));
        }
        const auto color = palette.color(selected);
        const auto selectionLabel = [&]() -> std::string {
            if (selected == EditorPalette::kSampled)
                return "Sampled color";
            const auto &entry = palette.entries()[static_cast<std::size_t>(selected)];
            return std::to_string(selected + 1) + "/" + std::to_string(palette.size()) + " " +
                   (entry.name_.empty() ? "Color" : entry.name_);
        };
        setLabelText(m_selected, clipped(selectionLabel()));
        setLabelText(
            m_rgb,
            "R " + std::to_string(color.red_) + "  G " + std::to_string(color.green_) + "  B " +
                std::to_string(color.blue_)
        );
        setTextInputValue(m_hex, paletteHex(color));
        setLabelText(m_pageLabel, std::to_string(m_page + 1) + " / " + std::to_string(pages));
        setDisabled(m_edit, selected == EditorPalette::kSampled);
        setDisabled(m_previous, m_page == 0);
        setDisabled(m_next, m_page + 1 == pages);
        m_lastSelected = selected;
        m_lastSize = palette.size();
        m_lastVisible = visible;
    }

    void selectSample(EditorPalette &palette, int &selected, IRMath::Color color) {
        selected = palette.sample(color);
        if (selected >= 0)
            m_page = selected / kPaletteCount;
        m_status = selected < 0 ? "Picked RGB / Add to save" : "Picked palette color";
        refresh(palette, selected);
    }

    IREntity::EntityId panel() const {
        return m_panel;
    }

    const std::array<IREntity::EntityId, kPaletteCount> &swatches() const {
        return m_swatches;
    }

    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }

    const std::string &status() const {
        return m_status;
    }

    bool containsCursor(IRMath::vec2 cursor) const {
        return widgetContainsCursor(m_panel, cursor);
    }

  private:
    IREntity::EntityId label(IRMath::ivec2 offset, const std::string &text) {
        const auto widget =
            IRPrefab::Widget::makeLabel(m_position + offset, text, {}, IRRender::TextSize::SMALL);
        m_widgets.push_back(widget);
        return widget;
    }

    IREntity::EntityId button(IRMath::ivec2 offset, IRMath::ivec2 size, const std::string &text) {
        const auto widget = IRPrefab::Widget::makeButton(
            m_position + offset,
            size,
            text,
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(widget);
        return widget;
    }

    std::string clipped(std::string text) const {
        const float width = static_cast<float>(m_size.x - 24);
        if (IRRender::measureText(text, 0, IRRender::TextSize::SMALL).x <= width)
            return text;
        while (!text.empty() &&
               IRRender::measureText(text + "...", 0, IRRender::TextSize::SMALL).x > width)
            text.pop_back();
        return text + "...";
    }

    IRMath::ivec2 m_position{};
    IRMath::ivec2 m_size{};
    int m_page = 0;
    int m_lastSelected = -1;
    int m_lastSize = -1;
    bool m_lastVisible = true;
    std::string m_status;
    std::vector<IREntity::EntityId> m_widgets;
    std::array<IREntity::EntityId, kPaletteCount> m_swatches{};
    IREntity::EntityId m_panel = IREntity::kNullEntity;
    IREntity::EntityId m_selected = IREntity::kNullEntity;
    IREntity::EntityId m_rgb = IREntity::kNullEntity;
    IREntity::EntityId m_previous = IREntity::kNullEntity;
    IREntity::EntityId m_next = IREntity::kNullEntity;
    IREntity::EntityId m_pageLabel = IREntity::kNullEntity;
    IREntity::EntityId m_hex = IREntity::kNullEntity;
    IREntity::EntityId m_edit = IREntity::kNullEntity;
    IREntity::EntityId m_add = IREntity::kNullEntity;
    IREntity::EntityId m_file = IREntity::kNullEntity;
    IREntity::EntityId m_load = IREntity::kNullEntity;
    IREntity::EntityId m_save = IREntity::kNullEntity;
    IREntity::EntityId m_defaults = IREntity::kNullEntity;
    IREntity::EntityId m_statusLabel = IREntity::kNullEntity;
};

} // namespace IRVoxelEditor

#endif
