#include <gtest/gtest.h>

#include "../../creations/editors/voxel_editor/construction_grid.hpp"

#include <set>
#include <tuple>

namespace {
using IRMath::vec3;
using IRMath::vec4;
using IRPrefab::WorldPrimitiveVertex;

TEST(DepthTestedPrimitives, GeometryUsesCameraPixelPhaseAtFractionalPan) {
    const IRMath::vec2 camera(-0.625f, -4.3125f);
    const IRMath::vec2 zoom(4.0f);
    const auto correction =
        IRPrefab::detail::primitiveCameraRasterCorrection(camera, zoom, IRMath::ivec2(2), nullptr);
    EXPECT_FLOAT_EQ(correction.x, 0.0f);
    const float expectedY = IRPlatform::kGfx.screenYDirection_ > 0 ? -0.5f : 0.5f;
    EXPECT_FLOAT_EQ(correction.y, expectedY);
    const auto whole = IRPrefab::detail::primitiveCameraRasterCorrection(
        IRMath::vec2(-2.0f, 3.0f),
        zoom,
        IRMath::ivec2(2),
        nullptr
    );
    EXPECT_EQ(whole, IRMath::vec2(0.0f));
    const IRMath::CameraRasterPhase frame{
        IRMath::dvec2(camera),
        IRMath::cameraZoomPitch(zoom),
        IRMath::dvec2(0.25, 0.5)
    };
    const auto continuous =
        IRPrefab::detail::primitiveCameraRasterCorrection(camera, zoom, IRMath::ivec2(2), &frame);
    EXPECT_FLOAT_EQ(continuous.x, -0.5f);
    EXPECT_FLOAT_EQ(continuous.y, expectedY);
}

TEST(DepthTestedPrimitives, SupportGridLiesOnBottomFaceAndCellBoundaries) {
    std::vector<WorldPrimitiveVertex> lines;
    const vec3 origin(-12.0f, 8.0f, 3.0f);
    IRVoxelEditor::appendConstructionGridLines(origin, {3, 5, 2}, lines);
    ASSERT_EQ(lines.size(), 20u);
    for (const auto &vertex : lines) {
        EXPECT_FLOAT_EQ(vertex.position_.z, 4.5f);
        EXPECT_GE(vertex.position_.x, -12.5f);
        EXPECT_LE(vertex.position_.x, -9.5f);
        EXPECT_GE(vertex.position_.y, 7.5f);
        EXPECT_LE(vertex.position_.y, 12.5f);
        EXPECT_FLOAT_EQ(vertex.position_.x - IRMath::floor(vertex.position_.x), 0.5f);
        EXPECT_FLOAT_EQ(vertex.position_.y - IRMath::floor(vertex.position_.y), 0.5f);
    }
}

TEST(DepthTestedPrimitives, BoxFacesCoverFullVoxelWithoutBackfaceAlphaOrDuplicateEdges) {
    for (float yaw : {0.0f, 0.4f, 1.2f, 2.6f, 4.1f, 5.6f}) {
        SCOPED_TRACE(yaw);
        std::vector<WorldPrimitiveVertex> triangles;
        std::vector<WorldPrimitiveVertex> lines;
        const vec3 center(3.0f, -8.0f, 2.0f);
        const vec3 half(0.5f, 1.5f, 2.5f);
        IRPrefab::appendVisibleBox(center, half, vec4(0.2f), vec4(0.8f), yaw, triangles, lines);
        ASSERT_EQ(triangles.size(), 18u);
        ASSERT_EQ(lines.size(), 18u);
        const vec3 depthAxis =
            IRMath::rotateVectorByQuat(vec3(1.0f), IRMath::quatAxisAngle(vec3(0, 0, 1), yaw));
        for (int face = 0; face < 3; ++face) {
            for (int index = 0; index < 6; ++index) {
                const auto point = triangles[face * 6 + index].position_ - center;
                // Each face must oppose the actual camera-depth direction.
                EXPECT_LT(point[face] * depthAxis[face], 0.0f);
                for (int axis = 0; axis < 3; ++axis)
                    EXPECT_FLOAT_EQ(IRMath::abs(point[axis]), half[axis]);
            }
        }
        std::set<std::tuple<float, float, float, float, float, float>> edges;
        for (std::size_t i = 0; i < lines.size(); i += 2) {
            auto a = lines[i].position_;
            auto b = lines[i + 1].position_;
            if (std::tie(a.x, a.y, a.z) > std::tie(b.x, b.y, b.z))
                std::swap(a, b);
            EXPECT_TRUE(edges.emplace(a.x, a.y, a.z, b.x, b.y, b.z).second);
        }
    }
}

TEST(DepthTestedPrimitives, WorldDepthSortsAtEveryYawAndRetainsSubdivisionScale) {
    for (float yaw : {0.0f, 0.37f, IRMath::kHalfPi, 3.4f}) {
        const vec3 depthAxis =
            IRMath::rotateVectorByQuat(vec3(1.0f), IRMath::quatAxisAngle(vec3(0, 0, 1), yaw));
        for (int subdivisions : {1, 2, 8}) {
            const auto nearDepth = IRPrefab::worldPrimitiveDepth(-depthAxis, yaw, subdivisions);
            const auto farDepth = IRPrefab::worldPrimitiveDepth(depthAxis, yaw, subdivisions);
            EXPECT_LT(nearDepth, farDepth);
            EXPECT_GT(nearDepth, 0.0f);
            EXPECT_LT(farDepth, 1.0f);
        }
    }
    const float center = IRPrefab::worldPrimitiveDepth(vec3(0), 0, 1);
    const float single = IRPrefab::worldPrimitiveDepth(vec3(1, 0, 0), 0, 1) - center;
    const float doubled = IRPrefab::worldPrimitiveDepth(vec3(1, 0, 0), 0, 2) - center;
    EXPECT_FLOAT_EQ(doubled, single * 2.0f);
}

TEST(DepthTestedPrimitives, CardinalSupportCornerMatchesLowerCornerRasterDepth) {
    // A floor corner is half a cell below the voxel center in every view axis.
    // Cardinal stage1 stores the corresponding lower-corner micro coordinate.
    const IRMath::ivec3 rasterCorner(0, 0, 2);
    for (int cardinal = 0; cardinal < 4; ++cardinal) {
        const float yaw = cardinal * IRMath::kHalfPi;
        const vec3 worldPoint = IRMath::rotateCardinalZInv(
            vec3(rasterCorner) - vec3(0.5f),
            IRMath::rasterYawCardinalIndex(yaw)
        );
        for (int subdivisions : {1, 2, 8}) {
            const int storedDepth =
                IRMath::pos3DtoDistance(rasterCorner * subdivisions) * IRRender::kDepthEncodeShift;
            const float expected =
                static_cast<float>(storedDepth - IRConstants::kTrixelDistanceMinDistance) /
                static_cast<float>(
                    IRConstants::kTrixelDistanceMaxDistance -
                    IRConstants::kTrixelDistanceMinDistance
                );
            EXPECT_FLOAT_EQ(
                IRPrefab::worldPrimitiveDepth(worldPoint, yaw, subdivisions, true),
                expected
            );
            EXPECT_LT(
                IRPrefab::worldPrimitiveDepth(worldPoint, yaw, subdivisions, false),
                expected
            );
        }
    }
}

} // namespace
