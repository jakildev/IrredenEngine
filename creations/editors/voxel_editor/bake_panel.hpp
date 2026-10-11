#ifndef IR_VOXEL_EDITOR_BAKE_PANEL_H
#define IR_VOXEL_EDITOR_BAKE_PANEL_H

#include <irreden/ir_math.hpp>

#include "anim_panel.hpp"

#include <iterator>

// GUI-canvas geometry and shape table of the BAKE panel (shape list, P1 / P2
// sliders, BAKE button). Shared by the editor, which builds the widgets and
// bakes from it, and the sessions, which aim a scripted drag and click at them
// and mirror the bake in their occupancy model — the same split
// recipes_panel.hpp makes.

namespace IRVoxelEditor {

constexpr IRMath::ivec2 kBakePanelPos{130, 342};
constexpr IRMath::ivec2 kBakePanelSize{120, 156};

constexpr IRMath::SDF::ShapeType kBakeShapeTypes[] = {
    IRMath::SDF::ShapeType::BOX,
    IRMath::SDF::ShapeType::SPHERE,
    IRMath::SDF::ShapeType::CYLINDER,
    IRMath::SDF::ShapeType::TORUS,
    IRMath::SDF::ShapeType::CONE,
    IRMath::SDF::ShapeType::ELLIPSOID,
};
// The shape list's initial row, and the row a bad selection falls back to.
constexpr int kBakeDefaultShapeRow = 1;
static_assert(kBakeDefaultShapeRow < static_cast<int>(std::size(kBakeShapeTypes)));

constexpr float kBakeParamSliderMin = 0.5f;
constexpr float kBakeParamSliderMax = 12.0f;
constexpr float kBakeParam1Default = 8.0f;
constexpr float kBakeParam2Default = 3.0f;

constexpr SliderGeometry kBakeParam1SliderGeometry{
    IRMath::ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 100), IRMath::ivec2(112, 14)
};
constexpr SliderGeometry kBakeParam2SliderGeometry{
    IRMath::ivec2(kBakePanelPos.x + 4, kBakePanelPos.y + 118), IRMath::ivec2(112, 14)
};

constexpr IRMath::ivec2 kBakeButtonPos{kBakePanelPos.x + 4, kBakePanelPos.y + 136};
constexpr IRMath::ivec2 kBakeButtonSize{112, 12};

constexpr IRMath::vec2 bakeButtonCenterGuiTrixel() {
    return IRMath::vec2(
        static_cast<float>(kBakeButtonPos.x) + static_cast<float>(kBakeButtonSize.x) / 2.0f,
        static_cast<float>(kBakeButtonPos.y) + static_cast<float>(kBakeButtonSize.y) / 2.0f
    );
}

// The SDF params the panel's P1 / P2 sliders mean for @p type.
inline IRMath::vec4 bakeShapeParams(IRMath::SDF::ShapeType type, float p1, float p2) {
    switch (type) {
    case IRMath::SDF::ShapeType::SPHERE:
        return IRMath::vec4(p1, 0.0f, 0.0f, 0.0f);
    case IRMath::SDF::ShapeType::TORUS:
        return IRMath::vec4(p1, p2, 0.0f, 0.0f);
    case IRMath::SDF::ShapeType::BOX:
    case IRMath::SDF::ShapeType::ELLIPSOID:
        return IRMath::vec4(p1 * 2.0f, p1 * 2.0f, p2 * 2.0f, 0.0f);
    case IRMath::SDF::ShapeType::CYLINDER:
    case IRMath::SDF::ShapeType::CONE:
    default:
        return IRMath::vec4(p1, p1, p2 * 2.0f, 0.0f);
    }
}

} // namespace IRVoxelEditor

#endif /* IR_VOXEL_EDITOR_BAKE_PANEL_H */
