#ifndef SHAPES_PANEL_H
#define SHAPES_PANEL_H

#include "primitive_shapes.hpp"
#include "integer_stepper.hpp"
#include "sculpt_panel.hpp"
#include "recipes_panel.hpp"
#include <irreden/render/widgets.hpp>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace IRVoxelEditor {
constexpr IRMath::ivec2 kShapesPanelPos{650, 180};
constexpr IRMath::ivec2 kShapesPanelSize{224, 326};
constexpr IRMath::ivec2 kShapesDropdownPos{kShapesPanelPos + IRMath::ivec2(12, 26)};
constexpr IRMath::ivec2 kShapesDropdownSize{200, 18};
constexpr int kShapesItemHeight = 16;
constexpr auto kShapesDimensionSize = kIntegerFieldSize;
constexpr IRMath::ivec2 kShapesSpawnPos{kShapesPanelPos + IRMath::ivec2(12, 246)};
constexpr IRMath::ivec2 kShapesSpawnSize{144, 18};
constexpr IRMath::ivec2 kShapeStampPos{kShapesPanelPos + IRMath::ivec2(12, 160)};
constexpr IRMath::ivec2 kShapeDragPos{kShapesPanelPos + IRMath::ivec2(112, 160)};
constexpr IRMath::ivec2 kShapeModeSize{94, 18};
constexpr IRMath::ivec2 shapeRowPos(int axis) {
    return kShapesPanelPos + IRMath::ivec2(116, 54 + axis * 24);
}
constexpr IRMath::ivec2 shapeDimensionPos(int axis) {
    return integerFieldPos(shapeRowPos(axis));
}
constexpr IRMath::ivec2 shapeStepPos(int axis, int direction) {
    return integerStepPos(shapeRowPos(axis), direction);
}
struct ShapeSpawnRequest {
    PrimitiveShape shape_;
    IRMath::ivec3 size_;
};

class ShapesPanel {
  public:
    void create(IREntity::EntityId boxFillButton) {
        using namespace IRPrefab::Widget;
        m_panel = makePanel(kShapesPanelPos, kShapesPanelSize, "SHAPES / NEW VOXEL PART", true, -1);
        IREntity::setComponent(m_panel, IRComponents::C_HitBox2DGui{kShapesPanelSize});
        m_widgets.push_back(m_panel);
        m_dropdown = makeDropdown(
            kShapesDropdownPos,
            kShapesDropdownSize,
            {"Box / Cube", "Sphere", "Cone", "Cylinder", "Pyramid"},
            0,
            kShapesItemHeight,
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_dropdown);
        constexpr const char *labels[] = {"LENGTH (X)", "WIDTH (Y)", "HEIGHT (Z)", "ABOVE FLOOR"};
        for (int axis = 0; axis < 4; ++axis) {
            m_widgets.push_back(makeLabel(
                {kShapesPanelPos.x + 12, shapeDimensionPos(axis).y + 4},
                labels[axis],
                {},
                IRRender::TextSize::SMALL
            ));
            const bool elevation = axis == 3;
            m_dimensions[axis].create(shapeRowPos(axis), elevation ? 0 : 8, elevation ? 0 : 1, 64);
            for (auto id : m_dimensions[axis].widgets())
                m_widgets.push_back(id);
        }
        m_stamp = makeButton(kShapeStampPos, kShapeModeSize, "[STAMP]", IRRender::TextSize::SMALL);
        m_drag = makeButton(kShapeDragPos, kShapeModeSize, "DRAG", IRRender::TextSize::SMALL);
        m_widgets.push_back(m_stamp);
        m_widgets.push_back(m_drag);
        m_hint = makeLabel(
            kShapesPanelPos + IRMath::ivec2(12, 190),
            "Click to place a shaped part.",
            {},
            IRRender::TextSize::SMALL
        );
        m_hint2 = makeLabel(
            kShapesPanelPos + IRMath::ivec2(12, 208),
            "Click again to stamp another.",
            {},
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_hint);
        m_widgets.push_back(m_hint2);
        m_widgets.push_back(boxFillButton);
        m_spawn = makeButton(
            kShapesSpawnPos,
            kShapesSpawnSize,
            "SPAWN AT CENTER",
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_spawn);
        m_close = makeButton(
            kShapesPanelPos + IRMath::ivec2(164, 246),
            {48, 18},
            "CLOSE",
            IRRender::TextSize::SMALL
        );
        m_widgets.push_back(m_close);
        m_status = makeLabel(
            kShapesPanelPos + IRMath::ivec2(12, 308),
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
        for (auto &dimension : m_dimensions)
            dimension.update();
        if (IRPrefab::Widget::wasClicked(m_stamp) || IRPrefab::Widget::wasClicked(m_drag)) {
            m_dragMode = IRPrefab::Widget::wasClicked(m_drag);
            IRPrefab::Widget::setButtonLabel(m_stamp, m_dragMode ? "STAMP" : "[STAMP]");
            IRPrefab::Widget::setButtonLabel(m_drag, m_dragMode ? "[DRAG]" : "DRAG");
            IRPrefab::Widget::setLabelText(
                m_hint,
                m_dragMode ? "Drag the base, then release." : "Click to place a shaped part."
            );
            IRPrefab::Widget::setLabelText(
                m_hint2,
                m_dragMode ? "Lift for height. Click to place." : "Click again to stamp another."
            );
        }
        return IRPrefab::Widget::wasClicked(m_spawn) ? std::optional{selection()} : std::nullopt;
    }
    ShapeSpawnRequest selection() const {
        const auto shape =
            static_cast<PrimitiveShape>(IRPrefab::Widget::dropdownSelectedIndex(m_dropdown));
        return {shape, {m_dimensions[0].value(), m_dimensions[1].value(), m_dimensions[2].value()}};
    }
    bool dragMode() const {
        return m_dragMode;
    }
    int placementHeight() const {
        return m_dimensions[3].value();
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
    IREntity::EntityId m_stamp = IREntity::kNullEntity;
    IREntity::EntityId m_drag = IREntity::kNullEntity;
    IREntity::EntityId m_hint = IREntity::kNullEntity;
    IREntity::EntityId m_hint2 = IREntity::kNullEntity;
    std::array<IntegerStepper, 4> m_dimensions;
    bool m_dragMode = false;
    std::vector<IREntity::EntityId> m_widgets;
};
} // namespace IRVoxelEditor
#endif
