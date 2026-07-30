#include <gtest/gtest.h>

#include <irreden/render/picking.hpp>

// `IRPrefab::Picking::aimIsoPixel` is the CPU mirror of the
// `worldPos3DToMouseScreenPx` -> `mouseWorldPos3DAtIsoDepth` round trip: the
// canvas-frame iso pixel a cursor aimed at a world point actually casts from.
// It is what anything predicting a scripted click must go through, so its
// lossiness is contractual and pinned here. Root cause + the measured table
// these cases reproduce: `docs/design/editor-authoring-friction.md` §M-2
// (#2575). The voxel editor's session shadow model is the consumer.

namespace {

using IRPrefab::Picking::aimIsoPixel;

constexpr IRMath::CardinalIndex kCardinal = IRMath::CardinalIndex::k0;

// The face-aim depth the session builder clicks at: far enough inside the cube
// that the picker's dominant-axis face derivation is stable, but well under the
// half-pixel bias the floor applies. Mirrors
// IRVoxelEditor::Session::kFaceAimDepth (session_builder.hpp) — a test under
// test/ can't include a creation header, so keep this value in sync by hand.
constexpr float kFaceAimDepth = 0.4f;

TEST(PickingAimIsoPixel, VoxelCentreLandsOnItsOwnPixel) {
    const IRMath::vec3 centre(7.0f, 7.0f, 11.0f);
    EXPECT_EQ(aimIsoPixel(centre, kCardinal), IRMath::ivec2(0, 8));
}

TEST(PickingAimIsoPixel, MinusXAndMinusYFaceAimsCollapseOntoOnePixel) {
    // The defect this helper exists to make predictable: at cardinal yaw the
    // iso equations give x and y coefficient 1, so a +-0.4 face offset moves
    // iso.x by less than the +0.5 cell-centre bias and the floor snaps both
    // aims back onto the voxel's own column. The picker receives byte-identical
    // input for the two, so no downstream rule can separate them.
    const IRMath::vec3 centre(7.0f, 7.0f, 11.0f);
    const IRMath::vec3 minusXAim = centre - IRMath::vec3(kFaceAimDepth, 0.0f, 0.0f);
    const IRMath::vec3 minusYAim = centre - IRMath::vec3(0.0f, kFaceAimDepth, 0.0f);

    EXPECT_EQ(aimIsoPixel(minusXAim, kCardinal), IRMath::ivec2(0, 8));
    EXPECT_EQ(aimIsoPixel(minusYAim, kCardinal), IRMath::ivec2(0, 8));
    EXPECT_EQ(aimIsoPixel(minusXAim, kCardinal), aimIsoPixel(minusYAim, kCardinal));
    EXPECT_EQ(aimIsoPixel(minusXAim, kCardinal), aimIsoPixel(centre, kCardinal));
}

TEST(PickingAimIsoPixel, MinusZFaceAimEscapesOntoItsOwnPixel) {
    // iso.y carries z with coefficient 2, so the same 0.4 offset becomes a 0.8
    // iso shift — large enough to cross the floor boundary. This is why -z
    // anchors keep working while -y anchors do not.
    const IRMath::vec3 centre(7.0f, 7.0f, 11.0f);
    const IRMath::vec3 minusZAim = centre - IRMath::vec3(0.0f, 0.0f, kFaceAimDepth);

    EXPECT_EQ(aimIsoPixel(minusZAim, kCardinal), IRMath::ivec2(0, 7));
    EXPECT_NE(aimIsoPixel(minusZAim, kCardinal), aimIsoPixel(centre, kCardinal));
}

TEST(PickingAimIsoPixel, CollapseIsNotSpecificToTheDiagonalCell) {
    // §M-2 notes the same collapse for a cell off the x == y diagonal (the
    // aim lands on that voxel's own centre pixel either way), so the defect
    // is a property of the projection, not of one measured coordinate.
    const IRMath::vec3 centre(7.0f, 6.0f, 11.0f);
    EXPECT_EQ(aimIsoPixel(centre, kCardinal), IRMath::ivec2(-1, 9));
    EXPECT_EQ(
        aimIsoPixel(centre - IRMath::vec3(kFaceAimDepth, 0.0f, 0.0f), kCardinal),
        IRMath::ivec2(-1, 9)
    );
    EXPECT_EQ(
        aimIsoPixel(centre - IRMath::vec3(0.0f, kFaceAimDepth, 0.0f), kCardinal),
        IRMath::ivec2(-1, 9)
    );
}

TEST(PickingAimIsoPixel, FloorsRatherThanTruncatesBelowTheOrigin) {
    // `mouseWorldPos3DAtIsoDepth` floors; truncation would fold the whole
    // (-1, 0) band onto pixel 0 and silently mis-aim every negative-iso cell.
    EXPECT_EQ(aimIsoPixel(IRMath::vec3(0.6f, 0.0f, 0.0f), kCardinal), IRMath::ivec2(-1, -1));
}

TEST(PickingAimIsoPixel, AppliesTheCardinalRotation) {
    // rotateCardinalZ((1,0,0), k90) == (0,-1,0), so aiming at (1,0,0) under a
    // quarter turn must reduce to aiming at (0,-1,0) at the cardinal.
    EXPECT_EQ(
        aimIsoPixel(IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::CardinalIndex::k90),
        aimIsoPixel(IRMath::vec3(0.0f, -1.0f, 0.0f), kCardinal)
    );
    EXPECT_NE(
        aimIsoPixel(IRMath::vec3(1.0f, 0.0f, 0.0f), IRMath::CardinalIndex::k90),
        aimIsoPixel(IRMath::vec3(1.0f, 0.0f, 0.0f), kCardinal)
    );
}

} // namespace
