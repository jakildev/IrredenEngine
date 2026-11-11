#ifndef SHAPE_ERASE_WORKFLOW_H
#define SHAPE_ERASE_WORKFLOW_H

#include "entity_scene.hpp"
#include "session_builder.hpp"
#include "shape_placement.hpp"
#include "shapes_panel.hpp"

#include <irreden/common/undo.hpp>
#include <cstring>

namespace IRVoxelEditor::Session {

enum class ShapeEraseShot {
    BUTTONS,
    STAMP_PREVIEW,
    STAMP_COMMIT,
    REPEAT_PREVIEW,
    REPEAT_COMMIT,
    UNDO_REPEAT,
    REDO_REPEAT,
    TOGGLE_DRAW,
    TOGGLE_ERASE_DRAG,
    FOOTPRINT,
    HEIGHT,
    CANCEL,
    FOOTPRINT_AGAIN,
    HEIGHT_AGAIN,
    DRAG_COMMIT,
    UNDO_DRAG,
    REDO_DRAG
};

struct ShapeEraseCheck {
    const char *label_;
    ShapeEraseShot shot_;
    unsigned erasedMasks_;
};

inline constexpr std::array kShapeEraseChecks{
    ShapeEraseCheck{"erase_button_preserves_stamp_dimensions", ShapeEraseShot::BUTTONS, 0},
    ShapeEraseCheck{"erase_stamp_preview_30", ShapeEraseShot::STAMP_PREVIEW, 0},
    ShapeEraseCheck{"erase_stamp_exact_mask_30", ShapeEraseShot::STAMP_COMMIT, 1},
    ShapeEraseCheck{"erase_repeat_preview_30", ShapeEraseShot::REPEAT_PREVIEW, 1},
    ShapeEraseCheck{"erase_repeat_exact_mask_30", ShapeEraseShot::REPEAT_COMMIT, 3},
    ShapeEraseCheck{"erase_undo_repeat", ShapeEraseShot::UNDO_REPEAT, 1},
    ShapeEraseCheck{"erase_redo_repeat", ShapeEraseShot::REDO_REPEAT, 3},
    ShapeEraseCheck{"erase_v_switches_to_draw", ShapeEraseShot::TOGGLE_DRAW, 3},
    ShapeEraseCheck{"erase_v_preserves_drag_dimensions", ShapeEraseShot::TOGGLE_ERASE_DRAG, 3},
    ShapeEraseCheck{"erase_drag_footprint_67", ShapeEraseShot::FOOTPRINT, 3},
    ShapeEraseCheck{"erase_drag_height_preview_67", ShapeEraseShot::HEIGHT, 3},
    ShapeEraseCheck{"erase_escape_no_mutation", ShapeEraseShot::CANCEL, 3},
    ShapeEraseCheck{"erase_drag_footprint_again_67", ShapeEraseShot::FOOTPRINT_AGAIN, 3},
    ShapeEraseCheck{"erase_drag_height_again_67", ShapeEraseShot::HEIGHT_AGAIN, 3},
    ShapeEraseCheck{"erase_drag_exact_mask_67", ShapeEraseShot::DRAG_COMMIT, 7},
    ShapeEraseCheck{"erase_undo_drag", ShapeEraseShot::UNDO_DRAG, 3},
    ShapeEraseCheck{"erase_redo_drag", ShapeEraseShot::REDO_DRAG, 7}
};

inline constexpr IRMath::ivec3 kShapeEraseSize{12, 12, 8};
inline constexpr IRMath::ivec3 kShapeEraseFirst{3, 3, 0};
inline constexpr IRMath::ivec3 kShapeEraseRepeat{8, 3, 0};
inline constexpr IRMath::ivec3 kShapeEraseDragStart{3, 8, 0};
inline constexpr IRMath::ivec3 kShapeEraseDragEnd{5, 9, 0};

inline float shapeEraseWorkflowYaw(int shot) {
    return (shot < 8 ? 30.0f : 67.0f) * IRMath::kPi / 180.0f;
}

struct ShapeEraseWorkflowState {
    IREntity::EntityId selected_ = IREntity::kNullEntity;
    IREntity::EntityId foreign_ = IREntity::kNullEntity;
    std::vector<IRComponents::C_Voxel> baseline_;
    std::vector<IRComponents::C_Voxel> foreignBaseline_;
};

inline IREntity::EntityId
seedShapeEraseWorkflow(ShapeEraseWorkflowState &state, EntityScene &scene) {
    if (state.selected_ != IREntity::kNullEntity)
        return state.selected_;
    scene.clear();
    const auto pivot = derivePartPivot(kShapeEraseSize);
    state.selected_ =
        scene.addVoxelPart(kShapeEraseSize, pivot, IRComponents::EntityAnchor::CENTER);
    state.foreign_ = scene.addVoxelPart(
        kShapeEraseSize,
        pivot + IRMath::vec3(18, 0, 0),
        IRComponents::EntityAnchor::CENTER
    );
    for (auto entity : {state.selected_, state.foreign_}) {
        auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(entity);
        auto records = set.mutableAuthoredRecords();
        for (std::size_t i = 0; i < records.size(); ++i) {
            records[i] = {};
            records[i].color_ = entity == state.selected_ ? IRMath::Color(72, 164, 133, 255)
                                                          : IRMath::Color(83, 113, 205, 255);
            records[i].reserved_ = static_cast<std::uint32_t>(i + 100);
        }
        for (const auto cell :
             {IRMath::ivec3(2, 2, 0), IRMath::ivec3(8, 2, 1), IRMath::ivec3(4, 8, 1)})
            records[IRMath::index3DtoIndex1D(cell, set.size_)].color_ = {44, 55, 66, 0};
        set.resyncAfterAuthoredEdits();
        auto &baseline = entity == state.selected_ ? state.baseline_ : state.foreignBaseline_;
        baseline.assign(records.begin(), records.end());
    }
    scene.select(0, false);
    IRPrefab::Undo::clear();
    return state.selected_;
}

namespace detail {

inline bool shapeEraseMaskContains(IRMath::ivec3 cell, unsigned masks) {
    const auto inside = [&](IRMath::ivec3 lower, IRMath::ivec3 size) {
        return cellInBounds(cell - lower, size);
    };
    return ((masks & 1) && inside({2, 2, -1}, {3, 3, 3})) ||
           ((masks & 2) && inside({7, 2, -1}, {3, 3, 3})) ||
           ((masks & 4) && inside({3, 8, 0}, {3, 2, 3}));
}

} // namespace detail

inline bool evaluateShapeEraseWorkflow(
    const ShapeEraseWorkflowState &state,
    const EntityScene &scene,
    const ShapesPanel &panel,
    ShapePlacement &placement,
    const void *context,
    std::string &actual
) {
    using Shot = ShapeEraseShot;
    const auto &check = *static_cast<const ShapeEraseCheck *>(context);
    if (state.selected_ == IREntity::kNullEntity || scene.parts().size() != 2) {
        actual = "expected selected and foreign fixture parts";
        return false;
    }
    const auto &set = IREntity::getComponent<IRComponents::C_VoxelSetNew>(state.selected_);
    const auto &foreign = IREntity::getComponent<IRComponents::C_VoxelSetNew>(state.foreign_);
    const auto records = set.authoredRecords();
    const auto foreignRecords = foreign.authoredRecords();
    int wrong = 0;
    int removed = 0;
    int actualRemoved = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const int index = static_cast<int>(i);
        const IRMath::ivec3 cell{
            index % set.size_.x,
            index / set.size_.x % set.size_.y,
            index / (set.size_.x * set.size_.y)
        };
        const bool erase = state.baseline_[i].color_.alpha_ != 0 &&
                           detail::shapeEraseMaskContains(cell, check.erasedMasks_);
        actualRemoved += state.baseline_[i].color_.alpha_ != 0 && records[i].color_.alpha_ == 0;
        if (erase) {
            wrong += records[i].color_.alpha_ != 0;
            ++removed;
        } else {
            auto expectedRecord = state.baseline_[i];
            // Neighbor face visibility is derived again when a cut exposes a retained cell.
            constexpr auto faceBits = IRComponents::VoxelFlags::kFaceOccludedMask;
            expectedRecord.flags_ =
                (expectedRecord.flags_ & ~faceBits) | (records[i].flags_ & faceBits);
            wrong += std::memcmp(&records[i], &expectedRecord, sizeof(IRComponents::C_Voxel)) != 0;
        }
    }
    const bool foreignUnchanged =
        foreignRecords.size() == state.foreignBaseline_.size() && std::memcmp(
                                                                      foreignRecords.data(),
                                                                      state.foreignBaseline_.data(),
                                                                      foreignRecords.size_bytes()
                                                                  ) == 0;
    const auto selection = panel.selection();
    const bool drag = static_cast<int>(check.shot_) >= static_cast<int>(Shot::TOGGLE_ERASE_DRAG);
    bool ok = wrong == 0 && foreignUnchanged && scene.selectedEntity() == state.selected_ &&
              panel.visible() && panel.eraseMode() == (check.shot_ != Shot::TOGGLE_DRAW) &&
              panel.dragMode() == drag && selection.shape_ == PrimitiveShape::CUBE &&
              selection.size_ == IRMath::ivec3(3) && panel.placementHeight() == 0;
    std::optional<ShapePlacementProposal> expected;
    ShapePlacementPhase phase = ShapePlacementPhase::HOVER;
    if (check.shot_ == Shot::STAMP_PREVIEW)
        expected = ShapePlacementProposal{PrimitiveShape::CUBE, {3, 3, 3}, {2, 2, -1}};
    if (check.shot_ == Shot::REPEAT_PREVIEW)
        expected = ShapePlacementProposal{PrimitiveShape::CUBE, {3, 3, 3}, {7, 2, -1}};
    if (check.shot_ == Shot::FOOTPRINT || check.shot_ == Shot::FOOTPRINT_AGAIN) {
        phase = ShapePlacementPhase::FOOTPRINT;
        expected = ShapePlacementProposal{PrimitiveShape::CUBE, {3, 2, 1}, {3, 8, 0}};
    }
    if (check.shot_ == Shot::HEIGHT || check.shot_ == Shot::HEIGHT_AGAIN) {
        phase = ShapePlacementPhase::HEIGHT;
        expected = ShapePlacementProposal{PrimitiveShape::CUBE, {3, 2, 3}, {3, 8, 0}};
    }
    std::size_t redVertices = 0;
    if (expected) {
        const auto &proposal = placement.proposal();
        ok = ok && proposal && proposal->shape_ == expected->shape_ &&
             proposal->lower_ == expected->lower_ && proposal->size_ == expected->size_ &&
             placement.phase() == phase;
        std::vector<IRPrefab::WorldPrimitiveVertex> triangles;
        std::vector<IRPrefab::WorldPrimitiveVertex> lines;
        const auto ray = IRRender::unprojectView(IRMath::vec2(0), 3);
        placement.appendAffectedCells(
            ray,
            deriveSceneOrigin(kShapeEraseSize),
            set.size_,
            [&](IRMath::ivec3 cell) {
                return records[IRMath::index3DtoIndex1D(cell, set.size_)].color_.alpha_ != 0;
            },
            triangles,
            lines
        );
        for (const auto &vertex : triangles)
            redVertices += vertex.color_.r > .9f && vertex.color_.g < .2f && vertex.color_.a > .4f;
        ok = ok && redVertices != 0 && redVertices == triangles.size();
    } else {
        ok = ok && !placement.proposal() && placement.phase() == ShapePlacementPhase::HOVER;
    }
    if (check.erasedMasks_ == 0)
        ok = ok && !IRPrefab::Undo::canUndo();
    else
        ok = ok && IRPrefab::Undo::canUndo();
    const bool redo = check.shot_ == Shot::UNDO_REPEAT || check.shot_ == Shot::UNDO_DRAG;
    ok = ok && IRPrefab::Undo::canRedo() == redo;
    actual = "wrong_cells=" + std::to_string(wrong) + " removed=" + std::to_string(removed) +
             " actual_removed=" + std::to_string(actualRemoved) +
             " foreign_unchanged=" + std::to_string(foreignUnchanged) +
             " red_vertices=" + std::to_string(redVertices) +
             " erase=" + std::to_string(panel.eraseMode()) +
             " drag=" + std::to_string(panel.dragMode()) +
             " phase=" + std::to_string(static_cast<int>(placement.phase()));
    if (placement.proposal()) {
        const auto &p = *placement.proposal();
        actual += " lower=" + std::to_string(p.lower_.x) + "," + std::to_string(p.lower_.y) + "," +
                  std::to_string(p.lower_.z) + " size=" + std::to_string(p.size_.x) + "," +
                  std::to_string(p.size_.y) + "," + std::to_string(p.size_.z);
    }
    return ok;
}

bool evaluateShapeEraseCheck(const void *context, std::string &actual);

inline Recipe buildShapesErase() {
    using Shot = ShapeEraseShot;
    using Event = IRVideo::GuiInputEvent;
    using Type = Event::Type;
    Recipe recipe;
    recipe.name_ = "shapes_erase";
    recipe.startWithEmptyComposite_ = true;
    for (const auto &check : kShapeEraseChecks) {
        Segment segment;
        segment.label_ = check.label_;
        segment.zoom_ = 6;
        segment.cameraIso_ = IRMath::rotate2D(
            IRMath::vec2(-45, 0),
            shapeEraseWorkflowYaw(static_cast<int>(&check - kShapeEraseChecks.data()))
        );
        int frame = 2;
        const auto emit = [&](Type type, IRInput::KeyMouseButtons key = IRInput::kNullButton) {
            segment.events_.push_back(Event{.frameOffset_ = frame, .type_ = type, .button_ = key});
            frame += 3;
        };
        const auto click = [&] {
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
        };
        const auto gui = [&](IRMath::vec2 point) {
            segment.guiAims_.push_back({static_cast<int>(segment.events_.size()), point});
            emit(Type::MOVE);
        };
        const auto widget = [&](IRMath::vec2 point) {
            gui(point);
            click();
        };
        const auto move = [&](IRMath::ivec3 local) {
            segment.aims_.push_back(
                {static_cast<int>(segment.events_.size()),
                 deriveSceneOrigin(kShapeEraseSize) + IRMath::vec3(local) - IRMath::vec3(0, 0, .5f)}
            );
            emit(Type::MOVE);
        };
        const auto tap = [&](IRInput::KeyMouseButtons key) {
            emit(Type::PRESS, key);
            emit(Type::RELEASE, key);
        };
        const auto park = [&] { gui(IRMath::vec2(kShapesPanelPos + IRMath::ivec2(20, 10))); };
        switch (check.shot_) {
        case Shot::BUTTONS:
            widget(sculptButtonCenter(11));
            for (int axis = 0; axis < 3; ++axis)
                for (int step = 0; step < 5; ++step)
                    widget(IRMath::vec2(shapeStepPos(axis, -1) + kIntegerStepSize / 2));
            widget(IRMath::vec2(kShapeErasePos + kShapeModeSize / 2));
            park();
            break;
        case Shot::STAMP_PREVIEW:
            move(kShapeEraseFirst);
            break;
        case Shot::REPEAT_PREVIEW:
            move(kShapeEraseRepeat);
            break;
        case Shot::STAMP_COMMIT:
            move(kShapeEraseFirst);
            click();
            park();
            break;
        case Shot::REPEAT_COMMIT:
            move(kShapeEraseRepeat);
            click();
            park();
            break;
        case Shot::DRAG_COMMIT:
            click();
            park();
            break;
        case Shot::UNDO_REPEAT:
        case Shot::UNDO_DRAG:
            widget(sculptButtonCenter(6));
            park();
            break;
        case Shot::REDO_REPEAT:
        case Shot::REDO_DRAG:
            widget(sculptButtonCenter(7));
            park();
            break;
        case Shot::TOGGLE_DRAW:
            tap(IRInput::kKeyButtonV);
            park();
            break;
        case Shot::TOGGLE_ERASE_DRAG:
            widget(IRMath::vec2(kShapeDragPos + kShapeModeSize / 2));
            tap(IRInput::kKeyButtonV);
            park();
            break;
        case Shot::FOOTPRINT:
        case Shot::FOOTPRINT_AGAIN:
            move(kShapeEraseDragStart);
            emit(Type::PRESS, IRInput::kMouseButtonLeft);
            move(kShapeEraseDragEnd);
            break;
        case Shot::HEIGHT:
        case Shot::HEIGHT_AGAIN:
            emit(Type::RELEASE, IRInput::kMouseButtonLeft);
            move(kShapeEraseDragEnd + IRMath::ivec3(0, 0, 2));
            break;
        case Shot::CANCEL:
            tap(IRInput::kKeyButtonEscape);
            park();
            break;
        }
        segment.assertions_.push_back(
            IRPrefab::GuiTest::predicate(&evaluateShapeEraseCheck, &check, check.label_)
        );
        recipe.segments_.push_back(std::move(segment));
    }
    return recipe;
}

} // namespace IRVoxelEditor::Session

#endif
