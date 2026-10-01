#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/render/picking.hpp>

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
