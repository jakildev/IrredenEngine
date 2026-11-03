#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/picking.hpp>

#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <tuple>

// `castGridRay` is the SCREEN_PIXEL cast's voxel-set traversal and the voxel
// editor's session shadow model: both ask it which cell, and which face of it,
// a view ray reaches first.

namespace {

using IRMath::ivec3;
using IRMath::vec3;
using IRPrefab::Picking::castGridRay;
using IRPrefab::Picking::GridRayHit;

const ivec3 kViewRay(1, 1, 1);
const ivec3 kGridSize(8, 8, 8);
const ivec3 kAnchor(4, 4, 4);

struct Cells {
    std::set<std::tuple<int, int, int>> occupied_;

    bool operator()(ivec3 cell) const {
        return occupied_.count({cell.x, cell.y, cell.z}) != 0;
    }
};

std::optional<GridRayHit> castThrough(const Cells &cells, vec3 point) {
    return castGridRay(point, kViewRay, kGridSize, cells);
}

// The three camera-facing faces of one voxel each resolve to their own normal
// when the ray passes through that face's centre.
TEST(PickingGridRay, EachCameraFacingFaceResolvesItsOwnNormal) {
    const Cells cells{{{4, 4, 4}}};
    const ivec3 normals[3] = {ivec3(-1, 0, 0), ivec3(0, -1, 0), ivec3(0, 0, -1)};
    for (const ivec3 &normal : normals) {
        const std::optional<GridRayHit> hit =
            castThrough(cells, vec3(kAnchor) + vec3(normal) * 0.5f);
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->cell_, kAnchor);
        EXPECT_EQ(hit->faceNormal_, normal);
    }
}

// The -x and -y face centres project one iso unit apart; a ray through each
// must separate them, and so must rays much closer to their shared edge.
TEST(PickingGridRay, MinusXAndMinusYSeparateEitherSideOfTheirSharedEdge) {
    const Cells cells{{{4, 4, 4}}};
    const vec3 edge = vec3(kAnchor) + vec3(-0.5f, -0.5f, 0.0f);
    const std::optional<GridRayHit> xSide = castThrough(cells, edge + vec3(0.0f, 0.05f, 0.0f));
    const std::optional<GridRayHit> ySide = castThrough(cells, edge + vec3(0.05f, 0.0f, 0.0f));
    ASSERT_TRUE(xSide.has_value());
    ASSERT_TRUE(ySide.has_value());
    EXPECT_EQ(xSide->faceNormal_, ivec3(-1, 0, 0));
    EXPECT_EQ(ySide->faceNormal_, ivec3(0, -1, 0));
}

// The entry face is the one the ray crosses, not the dominant axis of the
// offset from the voxel centre: this ray enters -x close to the +y edge, where
// the offset's largest component is +y.
TEST(PickingGridRay, EntryFaceIsNotTheDominantOffsetAxis) {
    const Cells cells{{{4, 4, 4}}};
    const std::optional<GridRayHit> hit =
        castThrough(cells, vec3(kAnchor) + vec3(-0.5f, 0.45f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->faceNormal_, ivec3(-1, 0, 0));
}

TEST(PickingGridRay, NearestOccupiedCellAlongTheRayWins) {
    const Cells cells{{{4, 4, 4}, {2, 2, 2}}};
    const std::optional<GridRayHit> hit =
        castThrough(cells, vec3(kAnchor) + vec3(0.1f, 0.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->cell_, ivec3(2, 2, 2));
}

// A chord that only clips a cell's corner is still a hit: the walk visits
// every cell the line crosses, however briefly.
TEST(PickingGridRay, ShortChordThroughACornerStillHits) {
    const Cells cells{{{4, 4, 4}}};
    // Enters -x at y = +0.49 and leaves through +y after 0.01 of travel.
    const std::optional<GridRayHit> hit =
        castThrough(cells, vec3(kAnchor) + vec3(-0.5f, 0.49f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->cell_, kAnchor);
}

TEST(PickingGridRay, RayPositionIsTheEntryPoint) {
    const Cells cells{{{4, 4, 4}}};
    const vec3 origin = vec3(kAnchor) + vec3(-0.5f, 0.1f, -0.2f) - vec3(3.0f);
    const std::optional<GridRayHit> hit = castThrough(cells, origin);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NEAR(hit->rayT_, 3.0f, 1e-4f);
}

TEST(PickingGridRay, MissesWhenNothingIsOccupiedOrTheLineIsOutside) {
    EXPECT_FALSE(castThrough(Cells{}, vec3(kAnchor)).has_value());
    const Cells cells{{{4, 4, 4}}};
    EXPECT_FALSE(castThrough(cells, vec3(40.0f, 0.0f, 0.0f)).has_value());
}

TEST(PickingGridRay, ContinuousDirectionResolvesEntryAndPreservesParameterScale) {
    const Cells cells{{{4, 4, 4}}};
    const vec3 face(4.0f, 3.5f, 4.0f);
    const vec3 direction(0.3660254f, 1.3660254f, 1.0f);
    const auto hit = castGridRay(face - direction * 7.0f, direction, kGridSize, cells);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell_, kAnchor);
    EXPECT_EQ(hit->faceNormal_, ivec3(0, -1, 0));
    EXPECT_NEAR(hit->rayT_, 7.0f, 0.0001f);
    const auto scaled = castGridRay(face - direction * 7.0f, direction * 2.0f, kGridSize, cells);
    ASSERT_TRUE(scaled);
    EXPECT_EQ(scaled->cell_, kAnchor);
    EXPECT_EQ(scaled->faceNormal_, hit->faceNormal_);
    EXPECT_NEAR(scaled->rayT_, 3.5f, 0.0001f);
}

TEST(PickingGridRay, ParallelAxesAtDiagonalOrbitDoNotSkipOrInventCells) {
    const Cells cells{{{4, 4, 4}, {4, 6, 6}}};
    const auto hit = castGridRay(vec3(4, 3.5f, 4), vec3(0, 1.4142136f, 1), kGridSize, cells);
    ASSERT_TRUE(hit);
    EXPECT_EQ(hit->cell_, kAnchor);
    EXPECT_EQ(hit->faceNormal_, ivec3(0, -1, 0));
    EXPECT_FALSE(castGridRay(vec3(8, 3.5f, 4), vec3(0, 1.4142136f, 1), kGridSize, cells));
    EXPECT_FALSE(castGridRay(vec3(4), vec3(0), kGridSize, cells));
}

TEST(PickingGridRay, ContinuousTraversalAgreesWithIndependentCellSlabs) {
    const Cells cells{{{1, 2, 3}, {3, 3, 4}, {4, 4, 4}, {6, 5, 2}, {2, 6, 6}}};
    const vec3 directions[] = {
        {0.3660254f, 1.3660254f, 1},
        {0, 1.4142136f, 1},
        {-0.3660254f, 1.3660254f, 1},
        {-1.4142136f, 0, 1},
        {-0.3660254f, -1.3660254f, 1},
        {1.4142136f, 0, 1}
    };
    for (vec3 direction : directions) {
        for (const auto &[x, y, z] : cells.occupied_) {
            const vec3 origin = vec3(x, y, z) + vec3(0.17f, -0.11f, 0.08f);
            float nearest = std::numeric_limits<float>::infinity();
            ivec3 expectedCell(0), expectedNormal(0);
            for (const auto &[cx, cy, cz] : cells.occupied_) {
                const vec3 center(cx, cy, cz);
                float enter = -std::numeric_limits<float>::infinity();
                float leave = std::numeric_limits<float>::infinity();
                ivec3 normal(0);
                for (int axis = 0; axis < 3; ++axis) {
                    const float offset = origin[axis] - center[axis];
                    if (direction[axis] == 0) {
                        if (offset < -0.5f || offset >= 0.5f)
                            leave = -std::numeric_limits<float>::infinity();
                        continue;
                    }
                    const float a = (-0.5f - offset) / direction[axis];
                    const float b = (0.5f - offset) / direction[axis];
                    if (IRMath::min(a, b) > enter) {
                        enter = IRMath::min(a, b);
                        normal = ivec3(0);
                        normal[axis] = direction[axis] > 0 ? -1 : 1;
                    }
                    leave = IRMath::min(leave, IRMath::max(a, b));
                }
                if (enter < leave && enter < nearest) {
                    nearest = enter;
                    expectedCell = ivec3(cx, cy, cz);
                    expectedNormal = normal;
                }
            }
            const auto hit = castGridRay(origin, direction, kGridSize, cells);
            ASSERT_TRUE(hit);
            EXPECT_EQ(hit->cell_, expectedCell);
            EXPECT_EQ(hit->faceNormal_, expectedNormal);
            EXPECT_NEAR(hit->rayT_, nearest, 0.0001f);
        }
    }
}

// IR_ASSERT throws in the test build; the traversal's per-step arithmetic is
// only valid for unit steps on every axis.
TEST(PickingGridRay, RejectsADirectionThatIsNotUnitOnEveryAxis) {
    const Cells cells{{{4, 4, 4}}};
    EXPECT_THROW(castGridRay(vec3(kAnchor), ivec3(1, 0, 1), kGridSize, cells), std::runtime_error);
    EXPECT_THROW(castGridRay(vec3(kAnchor), ivec3(2, 1, 1), kGridSize, cells), std::runtime_error);
}

// Under a quarter-turn the view ray's world direction flips sign on an axis,
// and the faces it can enter flip with it.
TEST(PickingGridRay, NegativeDirectionComponentEntersThePositiveFace) {
    const Cells cells{{{4, 4, 4}}};
    const std::optional<GridRayHit> hit =
        castGridRay(vec3(kAnchor) + vec3(0.5f, 0.0f, 0.0f), ivec3(-1, 1, 1), kGridSize, cells);
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->cell_, kAnchor);
    EXPECT_EQ(hit->faceNormal_, ivec3(1, 0, 0));
}

} // namespace
