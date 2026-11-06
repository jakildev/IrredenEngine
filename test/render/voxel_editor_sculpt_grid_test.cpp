#include "../../creations/editors/voxel_editor/sculpt_grid.hpp"
#include "../../creations/editors/voxel_editor/primitive_shapes.hpp"
#include "../../creations/editors/voxel_editor/editor_picking.hpp"

#include <irreden/entity/entity_manager.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
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

TEST(VoxelEditorSculptGrid, EnclosedFillKeepsTheSelectedRectangularSliceOnAllAxes) {
    const ivec3 size{5, 6, 7};
    for (int axis = 0; axis < 3; ++axis) {
        const int u = (axis + 1) % 3;
        const int v = (axis + 2) % 3;
        for (int slice : {0, size[axis] - 1}) {
            ivec3 seed(2);
            seed[axis] = slice;
            // The walls themselves may occupy the outermost cells.
            const auto region =
                IRVoxelEditor::enclosedPlaneCells(seed, size, axis, [&](ivec3 cell) {
                    EXPECT_TRUE(IRVoxelEditor::cellInBounds(cell, size));
                    EXPECT_EQ(cell[axis], slice);
                    return cell[u] == 0 || cell[v] == 0 || cell[u] == size[u] - 1 ||
                           cell[v] == size[v] - 1;
                });
            ASSERT_EQ(region.size(), static_cast<std::size_t>((size[u] - 2) * (size[v] - 2)));
            for (int row = 1; row < size[v] - 1; ++row) {
                for (int col = 1; col < size[u] - 1; ++col) {
                    auto cell = seed;
                    cell[u] = col;
                    cell[v] = row;
                    EXPECT_EQ(std::count(region.begin(), region.end(), cell), 1);
                }
            }
        }
    }
}

TEST(VoxelEditorSculptGrid, EnclosedFillRejectsAnOpenOutlineInEachDirection) {
    const ivec3 seed{3, 3, 0};
    const ivec3 size{7, 7, 1};
    for (const auto gap : {ivec3(1, 3, 0), ivec3(5, 3, 0), ivec3(3, 1, 0), ivec3(3, 5, 0)}) {
        const auto occupied = [&](ivec3 cell) {
            const auto delta = IRMath::abs(cell - seed);
            return cell != gap && IRMath::max(delta.x, delta.y) == 2;
        };
        EXPECT_TRUE(IRVoxelEditor::enclosedPlaneCells(seed, size, 2, occupied).empty());
    }
}

TEST(VoxelEditorSculptGrid, EnclosedFillDoesNotLeakThroughDiagonalContact) {
    const ivec3 seed{2, 2, 1};
    const auto region = IRVoxelEditor::enclosedPlaneCells(seed, ivec3(5), 2, [&](ivec3 cell) {
        const auto delta = IRMath::abs(cell - seed);
        return delta.x + delta.y == 1;
    });
    EXPECT_EQ(region, std::vector<ivec3>{seed});
}

TEST(VoxelEditorSculptGrid, EnclosedFillRejectsInvalidOccupiedAndExposedSeeds) {
    const ivec3 size{4, 5, 1};
    const auto unreachable = [](ivec3) {
        ADD_FAILURE() << "Invalid or exposed seed queried occupancy";
        return false;
    };
    for (const auto seed :
         {ivec3(-1, 2, 0), ivec3(4, 2, 0), ivec3(2, 2, 1), ivec3(0, 2, 0), ivec3(2, 4, 0)})
        EXPECT_TRUE(IRVoxelEditor::enclosedPlaneCells(seed, size, 2, unreachable).empty());
    for (int axis : {-1, 3})
        EXPECT_TRUE(
            IRVoxelEditor::enclosedPlaneCells(ivec3(2, 2, 0), size, axis, unreachable).empty()
        );
    EXPECT_TRUE(IRVoxelEditor::enclosedPlaneCells(ivec3(2, 2, 0), size, 2, [](ivec3) {
                    return true;
                }).empty());
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

TEST(VoxelEditorSculptGrid, EmptyFillSliceRejectsWithALinearProbe) {
    int probes = 0;
    EXPECT_TRUE(
        IRVoxelEditor::enclosedPlaneCells(ivec3(128, 128, 0), ivec3(256, 256, 1), 2, [&](ivec3) {
            ++probes;
            return false;
        }).empty()
    );
    EXPECT_LE(probes, 129);
}

TEST(VoxelEditorColorSample, DenseRayReturnsUnlitSourceColorOfNearestActiveCell) {
    std::vector<IRComponents::C_Voxel> records(3);
    records[0].color_ = {11, 22, 33, 0};
    records[1].color_ = {71, 82, 93, 255};
    records[2].color_ = {151, 162, 173, 255};
    const auto hit = IRVoxelEditor::detail::colorFromDenseGrid(
        vec3(10, 5, -20),
        vec3(0, 0, 1),
        vec3(10, 5, 3),
        ivec3(1, 1, 3),
        records
    );
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->color_.toPackedRGBA(), records[1].color_.toPackedRGBA());
    EXPECT_FLOAT_EQ(hit->rayT_, 23.5f);
    const auto reverse = IRVoxelEditor::detail::colorFromDenseGrid(
        vec3(10, 5, 20),
        vec3(0, 0, -1),
        vec3(10, 5, 3),
        ivec3(1, 1, 3),
        records
    );
    ASSERT_TRUE(reverse);
    EXPECT_EQ(reverse->color_.toPackedRGBA(), records[2].color_.toPackedRGBA());
    EXPECT_FALSE(
        IRVoxelEditor::detail::colorFromDenseGrid(
            vec3(12, 5, 0),
            vec3(0, 0, 1),
            vec3(10, 5, 3),
            ivec3(1, 1, 3),
            records
        )
    );
}

TEST(VoxelEditorColorSample, ReferencesParticipateWhileHiddenAndHelperEntitiesDoNot) {
    IREntity::EntityManager entities;
    const auto reference =
        IREntity::createEntity(IRComponents::C_VoxelSetNew{}, IRComponents::C_EditorReference{});
    EXPECT_TRUE(IRVoxelEditor::detail::colorSampleAllowed(reference));
    IREntity::getComponent<IRComponents::C_VoxelSetNew>(reference).visible_ = false;
    EXPECT_FALSE(IRVoxelEditor::detail::colorSampleAllowed(reference));
    IREntity::getComponent<IRComponents::C_VoxelSetNew>(reference).visible_ = true;
    IREntity::getComponent<IRComponents::C_VoxelSetNew>(reference).lodCulled_ = true;
    EXPECT_FALSE(IRVoxelEditor::detail::colorSampleAllowed(reference));
    IREntity::getComponent<IRComponents::C_VoxelSetNew>(reference).lodCulled_ = false;
    IREntity::setComponent(reference, IRComponents::C_EditorRecipeHandle{});
    EXPECT_FALSE(IRVoxelEditor::detail::colorSampleAllowed(reference));
    const auto privateSet = IREntity::createEntity(
        IRComponents::C_VoxelSetNew{},
        IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED}
    );
    EXPECT_FALSE(IRVoxelEditor::detail::colorSampleAllowed(privateSet));
}

TEST(VoxelEditorPrimitiveShapes, RequestedOccupiedBoundsSurviveThinOddAndEvenDimensions) {
    using IRVoxelEditor::PrimitiveShape;
    for (const auto size :
         {ivec3(1),
          ivec3(2),
          ivec3(3),
          ivec3(5),
          ivec3(1, 2, 17),
          ivec3(17, 1, 2),
          ivec3(2, 17, 1),
          ivec3(8, 6, 4)}) {
        for (const auto type :
             {PrimitiveShape::CUBE,
              PrimitiveShape::SPHERE,
              PrimitiveShape::CONE,
              PrimitiveShape::CYLINDER,
              PrimitiveShape::PYRAMID}) {
            ivec3 low = size, high(-1);
            for (int z = 0; z < size.z; ++z)
                for (int y = 0; y < size.y; ++y)
                    for (int x = 0; x < size.x; ++x) {
                        const ivec3 cell(x, y, z);
                        if (!IRVoxelEditor::primitiveShapeCell(type, cell, size))
                            continue;
                        low = IRMath::min(low, cell);
                        high = IRMath::max(high, cell);
                        EXPECT_TRUE(
                            IRVoxelEditor::primitiveShapeCell(
                                type,
                                ivec3(size.x - 1 - x, y, z),
                                size
                            )
                        );
                        EXPECT_TRUE(
                            IRVoxelEditor::primitiveShapeCell(
                                type,
                                ivec3(x, size.y - 1 - y, z),
                                size
                            )
                        );
                    }
            EXPECT_EQ(low, ivec3(0)) << static_cast<int>(type);
            EXPECT_EQ(high, size - ivec3(1)) << static_cast<int>(type);
        }
    }
}

TEST(VoxelEditorPrimitiveShapes, TaperedShapesWidenTowardPositiveZAndCubeFillsEveryCell) {
    using IRVoxelEditor::PrimitiveShape;
    const ivec3 size(5);
    for (const auto type : {PrimitiveShape::CONE, PrimitiveShape::PYRAMID}) {
        int first = 0, last = 0;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x) {
                first += IRVoxelEditor::primitiveShapeCell(type, {x, y, 0}, size);
                last += IRVoxelEditor::primitiveShapeCell(type, {x, y, 4}, size);
                for (int z = 0; z < 4; ++z)
                    if (IRVoxelEditor::primitiveShapeCell(type, {x, y, z}, size))
                        EXPECT_TRUE(IRVoxelEditor::primitiveShapeCell(type, {x, y, z + 1}, size));
            }
        EXPECT_LT(first, last);
        EXPECT_TRUE(IRVoxelEditor::primitiveShapeCell(type, {2, 2, 0}, size));
        EXPECT_FALSE(IRVoxelEditor::primitiveShapeCell(type, {0, 0, 0}, size));
    }
    for (int z = 0; z < 5; ++z)
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 5; ++x)
                EXPECT_TRUE(
                    IRVoxelEditor::primitiveShapeCell(PrimitiveShape::CUBE, {x, y, z}, size)
                );
}

TEST(VoxelEditorPrimitiveShapes, RejectsOutsideAndInvalidDimensions) {
    using IRVoxelEditor::PrimitiveShape;
    for (const auto type :
         {PrimitiveShape::CUBE,
          PrimitiveShape::SPHERE,
          PrimitiveShape::CONE,
          PrimitiveShape::CYLINDER,
          PrimitiveShape::PYRAMID}) {
        EXPECT_FALSE(IRVoxelEditor::primitiveShapeCell(type, ivec3(0), ivec3(0)));
        EXPECT_FALSE(IRVoxelEditor::primitiveShapeCell(type, ivec3(-1), ivec3(4)));
        EXPECT_FALSE(IRVoxelEditor::primitiveShapeCell(type, ivec3(4, 0, 0), ivec3(4)));
        EXPECT_FALSE(IRVoxelEditor::primitiveShapeCell(type, ivec3(0), ivec3(1, -1, 1)));
    }
    EXPECT_FALSE(
        IRVoxelEditor::primitiveShapeCell(static_cast<PrimitiveShape>(99), ivec3(0), ivec3(1))
    );
}

TEST(VoxelEditorPrimitiveShapes, ClosedBoundaryIntersectionsMatchSevenCellCounts) {
    constexpr std::array expected{343, 275, 167, 315, 215};
    for (int type = 0; type < 5; ++type) {
        int count = 0;
        for (int z = 0; z < 7; ++z)
            for (int y = 0; y < 7; ++y)
                for (int x = 0; x < 7; ++x)
                    count += IRVoxelEditor::primitiveShapeCell(
                        static_cast<IRVoxelEditor::PrimitiveShape>(type),
                        {x, y, z},
                        ivec3(7)
                    );
        EXPECT_EQ(count, expected[type]) << type;
    }
}

} // namespace
