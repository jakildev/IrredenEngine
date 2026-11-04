#ifndef SHAPES_WORKFLOW_H
#define SHAPES_WORKFLOW_H

#include "entity_scene.hpp"
#include "palette.hpp"
#include "sculpt_panel.hpp"
#include "session_builder.hpp"
#include "shapes_panel.hpp"

namespace IRVoxelEditor::Session {

struct ShapesWorkflowCheck {
    const char *label_;
    PrimitiveShape shape_;
    IRMath::ivec3 size_;
    int occupied_;
    int swatch_;
};

inline constexpr std::array kShapesWorkflowChecks{
    ShapesWorkflowCheck{"spawn_cube", PrimitiveShape::CUBE, {7, 7, 7}, 343, 4},
    ShapesWorkflowCheck{"spawn_sphere", PrimitiveShape::SPHERE, {7, 7, 7}, 275, 4},
    ShapesWorkflowCheck{"spawn_cone", PrimitiveShape::CONE, {7, 7, 7}, 167, 4},
    ShapesWorkflowCheck{"spawn_cylinder", PrimitiveShape::CYLINDER, {7, 7, 7}, 315, 4},
    ShapesWorkflowCheck{"spawn_pyramid", PrimitiveShape::PYRAMID, {7, 7, 7}, 215, 4},
    ShapesWorkflowCheck{"spawn_nonuniform_cube", PrimitiveShape::CUBE, {3, 4, 7}, 84, 6},
    ShapesWorkflowCheck{"spawn_thin_sphere", PrimitiveShape::SPHERE, {1, 2, 17}, 34, 2},
    ShapesWorkflowCheck{"undo_shape", PrimitiveShape::SPHERE, {1, 2, 17}, 0, 2},
    ShapesWorkflowCheck{"redo_shape", PrimitiveShape::SPHERE, {1, 2, 17}, 34, 2}
};

inline bool shapesWorkflowMatches(
    const EntityScene &scene, const ShapesWorkflowCheck &check, std::string &actual
) {
    const auto &parts = scene.parts();
    actual = "parts=" + std::to_string(parts.size());
    if (check.occupied_ == 0)
        return parts.empty();
    if (parts.size() != 1 || parts.front().kind_ != EditorPartKind::VOXEL_SET ||
        parts.front().mode_ != IRComponents::RotationMode::GRID)
        return false;
    const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(parts.front().entity_);
    if (set.size_ != check.size_) {
        actual += " wrong voxel dimensions";
        return false;
    }
    const auto color = kPaletteColors[check.swatch_];
    int occupied = 0;
    int wrongColor = 0;
    IRMath::ivec3 lower = check.size_;
    IRMath::ivec3 upper{-1};
    const auto records = set.authoredRecords();
    for (std::size_t index = 0; index < records.size(); ++index) {
        const auto seen = records[index].color_;
        if (seen.alpha_ == 0)
            continue;
        ++occupied;
        wrongColor += seen.red_ != color.red_ || seen.green_ != color.green_ ||
                      seen.blue_ != color.blue_ || seen.alpha_ != color.alpha_;
        const int i = static_cast<int>(index);
        const IRMath::ivec3 cell{
            i % set.size_.x,
            i / set.size_.x % set.size_.y,
            i / (set.size_.x * set.size_.y)
        };
        lower = IRMath::min(lower, cell);
        upper = IRMath::max(upper, cell);
    }
    actual += " occupied=" + std::to_string(occupied) +
              " expected=" + std::to_string(check.occupied_) +
              " wrongColor=" + std::to_string(wrongColor);
    return occupied == check.occupied_ && wrongColor == 0 && lower == IRMath::ivec3(0) &&
           upper == check.size_ - IRMath::ivec3(1);
}

bool evaluateShapesWorkflowCheck(const void *context, std::string &actual);

inline Recipe buildShapesWorkflow(IRMath::ivec3 size, IRMath::vec3 origin) {
    Builder builder("shapes_workflow", size, origin);
    IRMath::ivec3 previousSize{8};
    for (std::size_t index = 0; index < 7; ++index) {
        const auto &check = kShapesWorkflowChecks[index];
        builder.segment(check.label_, 8.0f);
        if (index == 0)
            builder.clickGui(sculptButtonCenter(11));
        else
            builder.clickGui(sculptButtonCenter(6));
        builder.selectPaletteSwatch(check.swatch_);
        builder.clickGui(IRMath::vec2(kShapesDropdownPos + kShapesDropdownSize / 2));
        builder.clickGui(
            IRMath::vec2(
                kShapesDropdownPos.x + kShapesDropdownSize.x / 2,
                kShapesDropdownPos.y + kShapesDropdownSize.y +
                    static_cast<int>(check.shape_) * kShapesItemHeight + kShapesItemHeight / 2
            )
        );
        for (int axis = 0; axis < 3; ++axis) {
            if (previousSize[axis] == check.size_[axis])
                continue;
            const auto field = IRMath::vec2(shapeDimensionPos(axis) + kShapesDimensionSize / 2);
            builder.clickGui(field);
            for (std::size_t digit = 0; digit < std::to_string(previousSize[axis]).size(); ++digit)
                builder.tapKey(IRInput::kKeyButtonBackspace);
            builder.typeText(field, std::to_string(check.size_[axis]));
        }
        previousSize = check.size_;
        builder.clickGui(IRMath::vec2(kShapesSpawnPos + kShapesSpawnSize / 2));
    }
    builder.segment(kShapesWorkflowChecks[7].label_, 8.0f);
    builder.clickGui(sculptButtonCenter(6));
    builder.segment(kShapesWorkflowChecks[8].label_, 8.0f);
    builder.clickGui(sculptButtonCenter(7));
    auto recipe = builder.finish();
    recipe.startWithEmptyComposite_ = true;
    for (std::size_t index = 0; index < recipe.segments_.size(); ++index)
        recipe.segments_[index].assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateShapesWorkflowCheck,
                &kShapesWorkflowChecks[index],
                kShapesWorkflowChecks[index].label_
            )
        );
    return recipe;
}

} // namespace IRVoxelEditor::Session
#endif
