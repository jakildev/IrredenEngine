#include <gtest/gtest.h>

#include "../../creations/editors/voxel_editor/array_preview.hpp"
#include <irreden/common/array_transforms.hpp>

namespace {

using namespace IRMath;
using namespace IRComponents;
using namespace IRVoxelEditor;

void expectNear(vec3 actual, vec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.0001f);
    EXPECT_NEAR(actual.y, expected.y, 0.0001f);
    EXPECT_NEAR(actual.z, expected.z, 0.0001f);
}

TEST(VoxelEditorArrayPreview, CopyCompositionPreservesSourceScaleAndParentFrame) {
    C_LocalTransform source{vec3(2, 3, 4), quatAxisAngle(vec3(1, 0, 0), kHalfPi), vec3(2)};
    source.unbounded_ = true;
    const C_LocalTransform offset{vec3(5, 0, 0), quatAxisAngle(vec3(0, 0, 1), kHalfPi)};
    const auto local = arrayCopyTransform(source, offset);
    expectNear(local.translation_, vec3(7, 3, 4));
    EXPECT_EQ(local.scale_, source.scale_);
    EXPECT_TRUE(local.unbounded_);
    expectNear(rotateVectorByQuat(vec3(0, 1, 0), local.rotation_), vec3(0, 0, 1));
    const C_WorldTransform parent{
        vec3(100, 200, 300),
        quatAxisAngle(vec3(0, 0, 1), kHalfPi),
        vec3(2)
    };
    const auto world = arrayCopyWorldTransform(source, offset, parent);
    expectNear(world.translation_, vec3(94, 214, 308));
    EXPECT_EQ(world.scale_, vec3(4));
    expectNear(source.translation_, vec3(2, 3, 4));
}

TEST(VoxelEditorArrayPreview, FullVoxelSurfaceOmitsInteriorAndIncludesEveryNewCopy) {
    const std::vector<C_Voxel> source(2, C_Voxel{Color{220, 40, 20, 255}});
    const auto offsets = IRPrefab::Arrays::linear(3, vec3(4, 0, 0));
    ArrayPreview preview;
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(2, 1, 1),
                        EntityAnchor::CORNER,
                        {},
                        {},
                        offsets,
                        RotationMode::GRID
                    )
                    .ok_);
    EXPECT_EQ(preview.copyCount(), 3);
    EXPECT_EQ(preview.cellCount(), 6u);
    EXPECT_EQ(preview.faceCount(), 30u);
    EXPECT_FALSE(preview.authoredTransformPreview());
    std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
    preview.append(0, triangles, lines);
    ASSERT_FALSE(triangles.empty());
    vec3 lo(1000), hi(-1000);
    for (const auto &vertex : triangles) {
        lo = min(lo, vertex.position_);
        hi = max(hi, vertex.position_);
        EXPECT_FLOAT_EQ(vertex.color_.a, 0.32f);
    }
    expectNear(lo, vec3(3.5f, -0.5f, -0.5f));
    expectNear(hi, vec3(13.5f, 0.5f, 0.5f));
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(source[0].color_.alpha_, 255);
    preview.clear();
    triangles.clear();
    lines.clear();
    preview.append(0, triangles, lines);
    EXPECT_TRUE(triangles.empty());
    EXPECT_TRUE(lines.empty());
    EXPECT_EQ(preview.copyCount(), 0);
}

TEST(VoxelEditorArrayPreview, EmptySourceCellsStayEmptyRatherThanBecomingBoundingBoxes) {
    std::vector<C_Voxel> source(3, C_Voxel{Color{0, 0, 0, 0}});
    source[0] = source[2] = C_Voxel{Color{20, 200, 40, 255}};
    ArrayPreview preview;
    const auto result = preview.rebuildVoxels(
        source,
        ivec3(3, 1, 1),
        EntityAnchor::CENTER,
        {},
        {},
        IRPrefab::Arrays::linear(2, vec3(0, 4, 0)),
        RotationMode::GRID
    );
    ASSERT_TRUE(result.ok_) << result.error_;
    EXPECT_EQ(preview.cellCount(), 4u);
    EXPECT_EQ(preview.faceCount(), 24u);
}

TEST(VoxelEditorArrayPreview, GroundAnchorAndScaledParentMoveWholeProspectiveBody) {
    const std::vector<C_Voxel> source(2, C_Voxel{Color{20, 100, 220, 255}});
    ArrayPreview preview;
    const C_WorldTransform parent{vec3(10, 20, 30), quatAxisAngle(vec3(0, 0, 1), kHalfPi), vec3(2)};
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(1, 1, 2),
                        EntityAnchor::GROUND,
                        C_LocalTransform{vec3(1, 0, 0)},
                        parent,
                        IRPrefab::Arrays::linear(1, vec3(2, 0, 0)),
                        RotationMode::DETACHED
                    )
                    .ok_);
    EXPECT_TRUE(preview.authoredTransformPreview());
    std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
    preview.append(0.3f, triangles, lines);
    vec3 lo(1000), hi(-1000);
    for (const auto &vertex : triangles) {
        lo = min(lo, vertex.position_);
        hi = max(hi, vertex.position_);
    }
    expectNear(lo, vec3(9, 25, 26));
    expectNear(hi, vec3(11, 27, 30));
}

TEST(VoxelEditorArrayPreview, GridRotationUsesRoundedSourceLatticeAtEvenCenterAnchor) {
    const std::vector<C_Voxel> source(2, C_Voxel{Color{200, 100, 20, 255}});
    ArrayPreview preview;
    const C_LocalTransform sourceTransform{vec3(10, 20, 0), quatAxisAngle(vec3(0, 0, 1), kHalfPi)};
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(2, 1, 1),
                        EntityAnchor::CENTER,
                        sourceTransform,
                        {},
                        std::vector<C_LocalTransform>{{vec3(0)}},
                        RotationMode::GRID
                    )
                    .ok_);
    EXPECT_EQ(preview.cellCount(), 2u);
    EXPECT_EQ(preview.faceCount(), 10u);
    std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
    preview.append(0, triangles, lines);
    vec3 lo(1000), hi(-1000);
    for (const auto &vertex : triangles) {
        lo = min(lo, vertex.position_);
        hi = max(hi, vertex.position_);
    }
    expectNear(lo, vec3(9.5f, 19.5f, -0.5f));
    expectNear(hi, vec3(10.5f, 21.5f, 0.5f));
}

TEST(VoxelEditorArrayPreview, RadialGuideSharesParentPlacementAndKeepsSourceOutsideCount) {
    const std::vector<C_Voxel> source(1, C_Voxel{Color{200, 100, 20, 255}});
    ArrayPreview preview;
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(1),
                        EntityAnchor::CENTER,
                        C_LocalTransform{vec3(3, 4, 0)},
                        {},
                        IRPrefab::Arrays::radial(6, vec3(0, 0, 1), 5),
                        RotationMode::GRID,
                        true,
                        5
                    )
                    .ok_);
    EXPECT_EQ(preview.copyCount(), 6);
    EXPECT_EQ(preview.cellCount(), 6u);
    std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
    preview.append(0, triangles, lines);
    ASSERT_EQ(lines.size(), 6u + 128u + 12u);
    for (std::size_t index = 6; index < 134; ++index)
        EXPECT_NEAR(length(lines[index].position_ - vec3(3, 4, 0)), 5, 0.0001f);
}

TEST(VoxelEditorArrayPreview, CornerAnchorRetainsNonzeroAuthoredBounds) {
    const std::vector<C_Voxel> source(1, C_Voxel{Color{200, 100, 20, 255}});
    ArrayPreview preview;
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(1),
                        EntityAnchor::CORNER,
                        {},
                        {},
                        IRPrefab::Arrays::linear(1, vec3(4, 0, 0)),
                        RotationMode::GRID,
                        false,
                        0,
                        vec3(-3, 2, 5)
                    )
                    .ok_);
    std::vector<IRPrefab::WorldPrimitiveVertex> triangles, lines;
    preview.append(0, triangles, lines);
    vec3 lo(1000), hi(-1000);
    for (const auto &vertex : triangles) {
        lo = min(lo, vertex.position_);
        hi = max(hi, vertex.position_);
    }
    expectNear(lo, vec3(0.5f, 1.5f, 4.5f));
    expectNear(hi, vec3(1.5f, 2.5f, 5.5f));
}

TEST(VoxelEditorArrayPreview, FailedRebuildClearsPriorProposalInsteadOfShowingPartialCopies) {
    const std::vector<C_Voxel> source(1, C_Voxel{Color{200, 100, 20, 255}});
    ArrayPreview preview;
    const auto offsets = IRPrefab::Arrays::linear(2, vec3(4, 0, 0));
    ASSERT_TRUE(preview
                    .rebuildVoxels(
                        source,
                        ivec3(1),
                        EntityAnchor::CENTER,
                        {},
                        {},
                        offsets,
                        RotationMode::GRID
                    )
                    .ok_);
    C_LocalTransform excessive;
    excessive.scale_ = vec3(10000);
    const auto failed = preview.rebuildVoxels(
        source,
        ivec3(1),
        EntityAnchor::CENTER,
        excessive,
        {},
        offsets,
        RotationMode::GRID
    );
    EXPECT_FALSE(failed.ok_);
    EXPECT_FALSE(failed.error_.empty());
    EXPECT_EQ(preview.copyCount(), 0);
    EXPECT_EQ(preview.cellCount(), 0u);
    EXPECT_EQ(preview.faceCount(), 0u);
}

} // namespace
