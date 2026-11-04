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

enum class ShapeGestureShot {
    STAMP_HOVER,
    STAMP_FIRST,
    STAMP_REPEAT,
    UNDO_REPEAT,
    FOOTPRINT,
    HEIGHT,
    DRAG_COMMIT,
    UNDO_DRAG,
    CANCEL_HEIGHT,
    RADIAL,
    CANCEL_RADIAL,
    PART_GRID,
    NEW_GRID
};
inline constexpr std::array kShapeGestureShots{
    ShapeGestureShot::STAMP_HOVER,
    ShapeGestureShot::STAMP_FIRST,
    ShapeGestureShot::STAMP_REPEAT,
    ShapeGestureShot::UNDO_REPEAT,
    ShapeGestureShot::FOOTPRINT,
    ShapeGestureShot::HEIGHT,
    ShapeGestureShot::DRAG_COMMIT,
    ShapeGestureShot::UNDO_DRAG,
    ShapeGestureShot::CANCEL_HEIGHT,
    ShapeGestureShot::RADIAL,
    ShapeGestureShot::CANCEL_RADIAL,
    ShapeGestureShot::PART_GRID,
    ShapeGestureShot::NEW_GRID
};
inline constexpr IRMath::ivec3 kStampStart{-3, 2, kSeedGroundPlaneWorldZ};
inline constexpr IRMath::ivec3 kStampRepeat{4, -3, kSeedGroundPlaneWorldZ};
inline constexpr IRMath::ivec3 kDragTip = kStampStart + IRMath::ivec3(3, 2, 0);

inline float shapeWorkflowYaw(int shot) {
    return (shot < 13 ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}

bool evaluateShapeGestureCheck(const void *context, std::string &actual);

inline void appendShapeGestures(Recipe &recipe) {
    using Shot = ShapeGestureShot;
    using Event = IRVideo::GuiInputEvent;
    using Type = Event::Type;
    constexpr std::array names{
        "stamp_hover_elevated",
        "stamp_first",
        "stamp_repeat",
        "undo_repeat",
        "drag_footprint",
        "drag_height",
        "drag_commit",
        "undo_drag",
        "cancel_height",
        "radial_footprint",
        "cancel_radial",
        "part_grid_new_prompt",
        "new_document_grid"
    };
    for (std::size_t i = 0; i < kShapeGestureShots.size(); ++i) {
        const auto shot = kShapeGestureShots[i];
        Segment segment;
        segment.label_ = names[i];
        segment.zoom_ = 8.0f;
        int frame = 0;
        const auto emit = [&](Type type, IRInput::KeyMouseButtons key = IRInput::kNullButton) {
            segment.events_.push_back(Event{.frameOffset_ = frame, .type_ = type, .button_ = key});
            frame += 2;
        };
        const auto click = [&] {
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
        };
        const auto widget = [&](IRMath::vec2 point) {
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            emit(Type::MOVE);
            click();
        };
        const auto move = [&](IRMath::ivec3 point) {
            segment.aims_.push_back(
                {static_cast<int>(segment.events_.size()),
                 IRMath::vec3(point) + IRMath::vec3(0, 0, 0.5f)}
            );
            emit(Type::MOVE);
        };
        const auto choose = [&](PrimitiveShape shape) {
            widget(IRMath::vec2(kShapesDropdownPos + kShapesDropdownSize / 2));
            widget(
                IRMath::vec2(
                    kShapesDropdownPos.x + kShapesDropdownSize.x / 2,
                    kShapesDropdownPos.y + kShapesDropdownSize.y +
                        (static_cast<int>(shape) + 0.5f) * kShapesItemHeight
                )
            );
        };
        const auto escape = [&] {
            emit(Type::PRESS, IRInput::kKeyButtonEscape);
            emit(Type::RELEASE, IRInput::kKeyButtonEscape);
        };
        switch (shot) {
        case Shot::STAMP_HOVER:
            widget(sculptButtonCenter(6));
            choose(PrimitiveShape::CUBE);
            // Previous shape is 1 x 2 x 17. Increment width and raise the stamp one cell.
            widget(IRMath::vec2(shapeStepPos(0, 1) + IRMath::ivec2(9)));
            widget(IRMath::vec2(shapeStepPos(3, 1) + IRMath::ivec2(9)));
            move(kStampStart - IRMath::ivec3(0, 0, 1));
            break;
        case Shot::STAMP_FIRST:
            move(kStampStart - IRMath::ivec3(0, 0, 1));
            click();
            break;
        case Shot::STAMP_REPEAT:
            move(kStampRepeat - IRMath::ivec3(0, 0, 1));
            click();
            break;
        case Shot::UNDO_REPEAT:
        case Shot::UNDO_DRAG:
            widget(sculptButtonCenter(6));
            break;
        case Shot::FOOTPRINT:
            widget(sculptButtonCenter(6));
            widget(IRMath::vec2(shapeStepPos(3, -1) + IRMath::ivec2(9)));
            widget(IRMath::vec2(kShapeDragPos + kShapeModeSize / 2));
            move(kStampStart);
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            move(kDragTip);
            break;
        case Shot::HEIGHT:
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
            move(kDragTip - IRMath::ivec3(0, 0, 3));
            break;
        case Shot::DRAG_COMMIT:
            click();
            break;
        case Shot::CANCEL_HEIGHT:
            move(kStampStart);
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            move(kDragTip);
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
            move(kDragTip - IRMath::ivec3(0, 0, 4));
            escape();
            break;
        case Shot::RADIAL:
            choose(PrimitiveShape::SPHERE);
            move(kStampStart);
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            move(kStampStart + IRMath::ivec3(2, 0, 0));
            break;
        case Shot::CANCEL_RADIAL:
            escape();
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
            break;
        case Shot::PART_GRID:
            widget(IRMath::vec2(partSizeStepPos(0, 1) + IRMath::ivec2(9)));
            widget(IRMath::vec2(partSizeStepPos(1, -1) + IRMath::ivec2(9)));
            widget(IRMath::vec2(kPartSizeCreatePos + kPartSizeCreateSize / 2));
            widget(IRMath::vec2(partSizeStepPos(2, 1) + kIntegerStepSize / 2));
            widget({526, 15});
            break;
        case Shot::NEW_GRID:
            widget({407, 229});
            break;
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(
                &evaluateShapeGestureCheck,
                &kShapeGestureShots[i],
                names[i]
            )
        );
        recipe.segments_.push_back(std::move(segment));
    }
}

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
        const auto point = IRMath::vec3(kStampStart) + IRMath::vec3(0, 0, 0.5f);
        builder.dragWorld(point, point);
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
    appendShapeGestures(recipe);
    // Keep the floor gestures in the unobstructed viewport beside the open Shapes panel.
    for (auto &segment : recipe.segments_)
        segment.cameraIso_ = {-24.0f, 4.0f};
    return recipe;
}

} // namespace IRVoxelEditor::Session
#endif
