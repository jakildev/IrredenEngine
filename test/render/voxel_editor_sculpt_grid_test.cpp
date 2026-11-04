#include "../../creations/editors/voxel_editor/sculpt_grid.hpp"
#include "../../creations/editors/voxel_editor/editor_picking.hpp"

#include <irreden/entity/entity_manager.hpp>

#include <gtest/gtest.h>

#include <vector>

namespace {

using IRMath::ivec3;
using IRMath::vec3;

TEST(VoxelEditorSculptGrid, EmptyFloorCanBePickedFromEveryCardinalView) {
    const ivec3 size{16, 12, 8};
    const ivec3 target{7, 4, 7};
    for (int x : {-1, 1}) {
        for (int y : {-1, 1}) {
            const vec3 direction{static_cast<float>(x), static_cast<float>(y), 1.0f};
            EXPECT_EQ(
                IRVoxelEditor::pickSculptPlane(
                    vec3(target) - direction * 30.0f,
                    direction,
                    size,
                    2,
                    7
                ),
                target
            );
        }
    }
}

TEST(VoxelEditorSculptGrid, RejectsOutsideBoundsAndParallelViews) {
    const ivec3 size{8, 8, 8};
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(-1, 0, 7), vec3(0, 0, 1), size, 2, 7));
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(8, 0, 7), vec3(0, 0, 1), size, 2, 7));
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(0), vec3(1, 1, 0), size, 2, 7));
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(0), vec3(1), size, 2, 8));
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(0), vec3(1), size, -1, 0));
}

TEST(VoxelEditorSculptGrid, CellEdgesUseRendererHalfUpConvention) {
    const ivec3 size{8, 8, 8};
    EXPECT_EQ(
        IRVoxelEditor::pickSculptPlane(vec3(-0.5f, 2.5f, 7), vec3(0, 0, 1), size, 2, 7),
        ivec3(0, 3, 7)
    );
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(-0.501f, 0, 7), vec3(0, 0, 1), size, 2, 7));
    EXPECT_FALSE(IRVoxelEditor::pickSculptPlane(vec3(7.5f, 0, 7), vec3(0, 0, 1), size, 2, 7));
}

TEST(VoxelEditorSculptGrid, FastStrokeHasNoGapsAndIsDirectionIndependent) {
    std::vector<ivec3> forward;
    std::vector<ivec3> backward;
    IRVoxelEditor::forEachStrokeCell(ivec3(1, 2, 7), ivec3(14, 9, 7), [&](ivec3 cell) {
        forward.push_back(cell);
    });
    IRVoxelEditor::forEachStrokeCell(ivec3(14, 9, 7), ivec3(1, 2, 7), [&](ivec3 cell) {
        backward.push_back(cell);
    });
    ASSERT_EQ(forward.size(), 14u);
    ASSERT_EQ(backward.size(), forward.size());
    for (std::size_t i = 0; i < forward.size(); ++i) {
        EXPECT_EQ(forward[i], backward[backward.size() - i - 1]);
        if (i != 0) {
            const ivec3 delta = forward[i] - forward[i - 1];
            EXPECT_LE(IRMath::abs(delta.x), 1);
            EXPECT_LE(IRMath::abs(delta.y), 1);
            EXPECT_EQ(delta.z, 0);
        }
    }
}

TEST(VoxelEditorSculptGrid, StationaryStrokeVisitsOneCell) {
    int count = 0;
    IRVoxelEditor::forEachStrokeCell(ivec3(2), ivec3(2), [&](ivec3 cell) {
        EXPECT_EQ(cell, ivec3(2));
        ++count;
    });
    EXPECT_EQ(count, 1);
}

TEST(VoxelEditorSculptGrid, HalfCellTiesAgreeInBothStrokeDirections) {
    for (const ivec3 origin : {ivec3(0), ivec3(-11, -5, 0)}) {
        std::vector<ivec3> forward;
        std::vector<ivec3> backward;
        const ivec3 end = origin + ivec3(22, 11, 0);
        IRVoxelEditor::forEachStrokeCell(origin, end, [&](ivec3 cell) { forward.push_back(cell); });
        IRVoxelEditor::forEachStrokeCell(end, origin, [&](ivec3 cell) {
            backward.push_back(cell);
        });
        ASSERT_EQ(forward.size(), backward.size());
        for (std::size_t i = 0; i < forward.size(); ++i)
            EXPECT_EQ(forward[i], backward[backward.size() - i - 1]);
    }
}

TEST(VoxelEditorSculptGrid, SurfaceSamplingPlaneKeepsTheAuthoredSlice) {
    const ivec3 size(16);
    const ivec3 target(6, 7, 8);
    // ADD and ERASE/PAINT sample opposite half-cell faces; inverted ADD
    // samples the clicked face one-and-a-half cells from its output slice.
    for (int axis = 0; axis < 3; ++axis) {
        for (float offset : {-1.5f, -0.5f, 0.5f, 1.5f}) {
            vec3 surface(target);
            surface[axis] += offset;
            for (int x : {-1, 1}) {
                for (int y : {-1, 1}) {
                    const vec3 direction(static_cast<float>(x), static_cast<float>(y), 1.0f);
                    const auto picked = IRVoxelEditor::pickSculptPlane(
                        surface - direction * 32.0f,
                        direction,
                        size,
                        axis,
                        target[axis],
                        offset
                    );
                    ASSERT_TRUE(picked);
                    EXPECT_EQ(*picked, target);
                    EXPECT_EQ((*picked)[axis], target[axis]);
                }
            }
        }
    }
}

TEST(VoxelEditorSculptGrid, SamplingTheVoxelCenterWouldShiftTheBrushRow) {
    const ivec3 size(16);
    const ivec3 target(7, 5, 14);
    // A cursor just to the near side of the floor-face center. Re-intersecting
    // its ray half a cell lower changes the rounded lateral cell coordinates.
    const vec3 surface(6.9f, 4.9f, 14.5f);
    const vec3 direction(1.0f);
    const vec3 rayOrigin = surface - direction * 32.0f;
    EXPECT_EQ(IRVoxelEditor::pickSculptPlane(rayOrigin, direction, size, 2, 14, 0.5f), target);
    EXPECT_NE(IRVoxelEditor::pickSculptPlane(rayOrigin, direction, size, 2, 14), target);

    // The outside face of the first/last slice is valid: bounds constrain the
    // authored voxel, not the continuous surface plane used for the ray.
    EXPECT_EQ(
        IRVoxelEditor::pickSculptPlane(vec3(5, 6, -0.5f), direction, size, 2, 0, -0.5f),
        ivec3(5, 6, 0)
    );
    EXPECT_EQ(
        IRVoxelEditor::pickSculptPlane(vec3(5, 6, 15.5f), direction, size, 2, 15, 0.5f),
        ivec3(5, 6, 15)
    );
}

TEST(VoxelEditorSculptGrid, PickingRefusesNonUnitWorldScaleBeforeTouchingVoxelStorage) {
    IREntity::EntityManager entities;
    const auto part = IREntity::createEntity(IRComponents::C_WorldTransform{});
    auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(part);
    world.translation_ = vec3(10, -3, 7);
    EXPECT_TRUE(IRVoxelEditor::isEditable(part));
    for (const vec3 scale : {vec3(2), vec3(1, 2, 1), vec3(0.5f), vec3(-1, 1, 1), vec3(0)}) {
        world.scale_ = scale;
        EXPECT_FALSE(IRVoxelEditor::isEditable(part));
        EXPECT_FALSE(IRVoxelEditor::pickEditablePlane(part, 2, 0));
    }
    world.scale_ = vec3(1);
    EXPECT_TRUE(IRVoxelEditor::isEditable(part));
    world.rotation_ = IRMath::vec4(0, 0, 1, 0);
    EXPECT_FALSE(IRVoxelEditor::isEditable(part));
}

} // namespace
