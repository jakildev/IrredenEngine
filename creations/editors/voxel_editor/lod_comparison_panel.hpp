#ifndef IR_VOXEL_EDITOR_LOD_COMPARISON_PANEL_H
#define IR_VOXEL_EDITOR_LOD_COMPARISON_PANEL_H

#include "lod_preview.hpp"
#include <irreden/render/widgets.hpp>

namespace IRVoxelEditor {

enum class LodComparisonAction { NONE, REFRESH, DERIVE, DONE };

// The image widgets contain captured pixels, not live secondary renderers.
// Tile extents never fit a tier's occupied bounds; clipping is labelled.
class LodComparisonPanel {
  public:
    bool create(IRMath::ivec2 origin, IRMath::ivec2 tileSize = {128, 96}) {
        if (!m_widgets.empty() || tileSize.x < 128 || tileSize.y < 48 || tileSize.x > 256 ||
            tileSize.y > 192)
            return false;
        using namespace IRPrefab::Widget;
        m_tileSize = tileSize;
        const int width = (tileSize.x + 8) * 5 + 8;
        const int height = tileSize.y * 3 + 178;
        m_widgets.push_back(
            makePanel(origin, {width, height}, "LOD COMPARISON / COMMON WORLD FRAME")
        );
        constexpr std::array labels{"REFRESH", "COPY TO TIER", "DONE"};
        for (std::size_t i = 0; i < m_buttons.size(); ++i) {
            m_buttons[i] = makeButton(
                origin + IRMath::ivec2(8 + static_cast<int>(i) * 110, 20),
                {104, 18},
                labels[i],
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_buttons[i]);
        }
        for (std::size_t tier = 0; tier < 5; ++tier) {
            const int x = 8 + static_cast<int>(tier) * (tileSize.x + 8);
            m_nativeLabels[tier] = makeLabel(
                origin + IRMath::ivec2(x, 46),
                "LOD" + std::to_string(tier) + " / NATIVE",
                {},
                IRRender::TextSize::SMALL
            );
            m_scaledLabels[tier] = makeLabel(
                origin + IRMath::ivec2(x, tileSize.y + 72),
                "LOD" + std::to_string(tier) + " / COMMON",
                {},
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_nativeLabels[tier]);
            m_widgets.push_back(m_scaledLabels[tier]);
            m_native[tier] = imageWidget(origin + IRMath::ivec2(x, 58), tileSize);
            m_scaled[tier] = imageWidget(origin + IRMath::ivec2(x, tileSize.y + 84), tileSize);
            m_widgets.push_back(m_native[tier]);
            m_widgets.push_back(m_scaled[tier]);
        }
        const int bottom = tileSize.y * 2 + 104;
        m_widgets.push_back(makeLabel(
            origin + IRMath::ivec2(8, bottom),
            "ONION / CYAN REF + MAGENTA SELECTED",
            {},
            IRRender::TextSize::SMALL
        ));
        m_onion = imageWidget(origin + IRMath::ivec2(8, bottom + 14), tileSize);
        m_widgets.push_back(m_onion);
        const int controlsX = tileSize.x + 24;
        m_widgets.push_back(makeLabel(
            origin + IRMath::ivec2(controlsX, bottom + 16),
            "SELECTED TIER",
            {},
            IRRender::TextSize::SMALL
        ));
        m_selected = makeSlider(
            origin + IRMath::ivec2(controlsX, bottom + 28),
            {220, 16},
            "",
            0,
            4,
            0,
            IRRender::TextSize::SMALL,
            {.step_ = 1}
        );
        m_widgets.push_back(makeLabel(
            origin + IRMath::ivec2(controlsX, bottom + 48),
            "REFERENCE TIER",
            {},
            IRRender::TextSize::SMALL
        ));
        m_reference = makeSlider(
            origin + IRMath::ivec2(controlsX, bottom + 60),
            {220, 16},
            "",
            0,
            4,
            1,
            IRRender::TextSize::SMALL,
            {.step_ = 1}
        );
        m_widgets.push_back(makeLabel(
            origin + IRMath::ivec2(controlsX + 240, bottom + 16),
            "SELECTED OPACITY",
            {},
            IRRender::TextSize::SMALL
        ));
        m_opacity = makeSlider(
            origin + IRMath::ivec2(controlsX + 240, bottom + 28),
            {180, 16},
            "",
            0,
            1,
            .5f,
            IRRender::TextSize::SMALL,
            {.step_ = .05f}
        );
        m_widgets.insert(m_widgets.end(), {m_selected, m_reference, m_opacity});
        m_status = makeLabel(
            origin + IRMath::ivec2(8, height - 18),
            "CAPTURING / EDITING PAUSED",
            {},
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_status);
        setVisible(false);
        return true;
    }

    void setVisible(bool visible) const {
        for (const auto id : m_widgets)
            IRPrefab::Widget::setVisible(id, visible);
    }
    void setStatus(const std::string &text) const {
        IRPrefab::Widget::setLabelText(m_status, text);
    }
    bool setImages(const LodCapturePlan &captures, int nativePhysicalPixelsPerGuiTexel = 1) {
        if (!captures.complete() || m_widgets.empty() || nativePhysicalPixelsPerGuiTexel < 1 ||
            nativePhysicalPixelsPerGuiTexel > 64 ||
            captures.images().back().size_.x < nativePhysicalPixelsPerGuiTexel ||
            captures.images().back().size_.y < nativePhysicalPixelsPerGuiTexel)
            return false;
        int divisor = 1;
        while (captures.finestSize().x / divisor > m_tileSize.x ||
               captures.finestSize().y / divisor > m_tileSize.y)
            divisor *= 2;
        for (std::size_t tier = 0; tier < 5; ++tier) {
            const auto native = IRRender::presentComparisonImage(
                captures.images()[tier],
                m_tileSize,
                1,
                {16, 20, 24, 255},
                nativePhysicalPixelsPerGuiTexel
            );
            auto scaled = IRRender::presentComparisonImage(
                captures.images()[tier],
                m_tileSize,
                1 << tier,
                {16, 20, 24, 255},
                divisor
            );
            setImage(m_native[tier], native.image_);
            setImage(m_scaled[tier], scaled.image_);
            m_normalized[tier] = std::move(scaled.image_);
            IRPrefab::Widget::setLabelText(
                m_nativeLabels[tier],
                "L" + std::to_string(tier) + " Z" +
                    std::to_string(static_cast<int>(kLodPreviewZooms[tier])) + " NATIVE" +
                    (native.clipped_ ? " CROP" : "")
            );
            IRPrefab::Widget::setLabelText(
                m_scaledLabels[tier],
                "LOD" + std::to_string(tier) + " " + std::to_string(1 << tier) + "/" +
                    std::to_string(divisor) + "X" + (scaled.clipped_ ? " CLIPPED" : " / COMMON")
            );
        }
        m_haveImages = true;
        m_lastSelected = -1;
        updateSelection();
        setStatus(
            captures.captureClipped()
                ? "APERTURE CLIPS MODEL / COPY PRESERVES EXTENT / OVERLAPPING BANDS STAY VISIBLE"
                : "CAPTURED / COPY PRESERVES EXTENT / OVERLAPPING BANDS STAY VISIBLE"
        );
        return true;
    }
    void updateSelection() {
        if (!m_haveImages)
            return;
        const int selected = static_cast<int>(selectedTier());
        const int reference = static_cast<int>(referenceTier());
        const float blend = opacity();
        if (selected == m_lastSelected && reference == m_lastReference && blend == m_lastOpacity)
            return;
        setImage(
            m_onion,
            IRRender::onionComparisonImages(m_normalized[reference], m_normalized[selected], blend)
        );
        m_lastSelected = selected;
        m_lastReference = reference;
        m_lastOpacity = blend;
    }
    LodComparisonAction poll() const {
        constexpr std::array actions{
            LodComparisonAction::REFRESH,
            LodComparisonAction::DERIVE,
            LodComparisonAction::DONE
        };
        for (std::size_t i = 0; i < m_buttons.size(); ++i)
            if (IRPrefab::Widget::wasClicked(m_buttons[i]))
                return actions[i];
        return LodComparisonAction::NONE;
    }
    IRRender::LodLevel selectedTier() const {
        return static_cast<IRRender::LodLevel>(
            IRMath::roundHalfUp(IRPrefab::Widget::sliderValue(m_selected))
        );
    }
    IRRender::LodLevel referenceTier() const {
        return static_cast<IRRender::LodLevel>(
            IRMath::roundHalfUp(IRPrefab::Widget::sliderValue(m_reference))
        );
    }
    float opacity() const {
        return IRPrefab::Widget::sliderValue(m_opacity);
    }
    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }

  private:
    static IREntity::EntityId imageWidget(IRMath::ivec2 origin, IRMath::ivec2 size) {
        IRComponents::C_WidgetImage image;
        image.texelSize_ = size;
        image.texels_.assign(
            static_cast<std::size_t>(size.x) * size.y,
            IRMath::Color{16, 20, 24, 255}
        );
        image.loaded_ = true;
        return IREntity::createEntity(
            IRComponents::C_Widget{IRComponents::WidgetKind::IMAGE, size},
            IRComponents::C_GuiPosition{origin},
            IRComponents::C_GuiElement{},
            std::move(image)
        );
    }
    static void setImage(IREntity::EntityId entity, const IRRender::ComparisonImage &source) {
        auto &image = IREntity::getComponent<IRComponents::C_WidgetImage>(entity);
        image.texelSize_ = source.size_;
        image.texels_ = source.pixels_;
        image.loaded_ = source.valid();
    }
    std::vector<IREntity::EntityId> m_widgets;
    std::array<IREntity::EntityId, 3> m_buttons{};
    std::array<IREntity::EntityId, 5> m_native{}, m_scaled{}, m_nativeLabels{}, m_scaledLabels{};
    std::array<IRRender::ComparisonImage, 5> m_normalized;
    IREntity::EntityId m_onion{}, m_selected{}, m_reference{}, m_opacity{}, m_status{};
    IRMath::ivec2 m_tileSize{0};
    int m_lastSelected = -1, m_lastReference = -1;
    float m_lastOpacity = -1;
    bool m_haveImages = false;
};

} // namespace IRVoxelEditor
#endif /* IR_VOXEL_EDITOR_LOD_COMPARISON_PANEL_H */
