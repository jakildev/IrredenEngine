#ifndef SHAPES_PANEL_H
#define SHAPES_PANEL_H

#include "primitive_shapes.hpp"
#include "recipes_panel.hpp"
#include <irreden/render/widgets.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace IRVoxelEditor {
constexpr IRMath::ivec2 kShapesPanelPos{650, 180};
constexpr IRMath::ivec2 kShapesPanelSize{224, 206};
constexpr IRMath::ivec2 kShapesDropdownPos{kShapesPanelPos + IRMath::ivec2(12, 26)};
constexpr IRMath::ivec2 kShapesDropdownSize{200, 18};
constexpr int kShapesItemHeight = 16;
constexpr IRMath::ivec2 kShapesDimensionSize{88, 18};
constexpr IRMath::ivec2 kShapesSpawnPos{kShapesPanelPos + IRMath::ivec2(12, 158)};
constexpr IRMath::ivec2 kShapesSpawnSize{144, 18};
constexpr IRMath::ivec2 shapeDimensionPos(int axis) {
    return kShapesPanelPos + IRMath::ivec2(124, 62 + axis * 28);
}
struct ShapeSpawnRequest {
    PrimitiveShape shape_;
    IRMath::ivec3 size_;
};

class ShapesPanel {
  public:
    void create() {
        using namespace IRPrefab::Widget;
        m_panel = makePanel(kShapesPanelPos, kShapesPanelSize, "SHAPES / NEW VOXEL PART");
        IREntity::setComponent(m_panel, IRComponents::C_HitBox2DGui{kShapesPanelSize});
        m_widgets.push_back(m_panel);
        m_dropdown = makeDropdown(
            kShapesDropdownPos,
            kShapesDropdownSize,
            {"Cube", "Sphere", "Cone", "Cylinder", "Pyramid"},
            0,
            kShapesItemHeight,
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_dropdown);
        constexpr const char *labels[] = {"LENGTH (X)", "WIDTH (Y)", "HEIGHT (Z)"};
        for (int axis = 0; axis < 3; ++axis) {
            m_widgets.push_back(makeLabel(
                {kShapesPanelPos.x + 12, shapeDimensionPos(axis).y + 4},
                labels[axis],
                {},
                IRRender::TextSize::SMALL
            ));
            m_dimensions[axis] = makeNumberField(
                shapeDimensionPos(axis),
                kShapesDimensionSize,
                8.0f,
                1.0f,
                64.0f,
                1.0f,
                IRRender::TextSize::SMALL
            );
            m_widgets.push_back(m_dimensions[axis]);
        }
        m_widgets.push_back(makeLabel(
            kShapesPanelPos + IRMath::ivec2(12, 142),
            "Voxel dimensions: 1-64",
            {},
            IRRender::TextSize::SMALL
        ));
        m_spawn = makeButton(
            kShapesSpawnPos,
            kShapesSpawnSize,
            "SPAWN NEW PART",
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_spawn);
        m_close = makeButton(
            kShapesPanelPos + IRMath::ivec2(164, 158),
            {48, 18},
            "CLOSE",
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_close);
        m_status = makeLabel(
            kShapesPanelPos + IRMath::ivec2(12, 186),
            "Uses the active color",
            {},
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_status);
        setVisible(false);
    }

    std::optional<ShapeSpawnRequest> update() {
        if (!visible())
            return std::nullopt;
        if (IRPrefab::Widget::wasClicked(m_close)) {
            setVisible(false);
            return std::nullopt;
        }
        if (!IRPrefab::Widget::wasClicked(m_spawn))
            return std::nullopt;
        constexpr PrimitiveShape shapes[] = {
            PrimitiveShape::CUBE,
            PrimitiveShape::SPHERE,
            PrimitiveShape::CONE,
            PrimitiveShape::CYLINDER,
            PrimitiveShape::PYRAMID
        };
        const int row = IRPrefab::Widget::dropdownSelectedIndex(m_dropdown);
        if (row < 0 || row >= 5)
            return std::nullopt;
        IRMath::ivec3 size;
        for (int axis = 0; axis < 3; ++axis)
            size[axis] = static_cast<int>(
                IRMath::roundHalfUp(IRPrefab::Widget::numberFieldValue(m_dimensions[axis]))
            );
        return ShapeSpawnRequest{shapes[row], size};
    }

    void setVisible(bool visible) {
        for (auto widget : m_widgets)
            IRPrefab::Widget::setVisible(widget, visible);
    }
    bool visible() const {
        return m_panel != IREntity::kNullEntity && IRPrefab::Widget::isVisible(m_panel);
    }
    bool containsCursor(IRMath::vec2 cursor) const {
        return visible() && guiPointInRect(cursor, kShapesPanelPos, kShapesPanelSize);
    }
    const std::vector<IREntity::EntityId> &widgets() const {
        return m_widgets;
    }
    void showStatus(std::string text) {
        while (!text.empty() && IRRender::measureText(text, 0, IRRender::TextSize::SMALL).x > 200)
            text.pop_back();
        IRPrefab::Widget::setLabelText(m_status, std::move(text));
    }

  private:
    IREntity::EntityId m_panel = IREntity::kNullEntity;
    IREntity::EntityId m_dropdown = IREntity::kNullEntity;
    IREntity::EntityId m_spawn = IREntity::kNullEntity;
    IREntity::EntityId m_close = IREntity::kNullEntity;
    IREntity::EntityId m_status = IREntity::kNullEntity;
    std::array<IREntity::EntityId, 3> m_dimensions{};
    std::vector<IREntity::EntityId> m_widgets;
};
} // namespace IRVoxelEditor
#endif
