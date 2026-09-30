#include <gtest/gtest.h>

#include <irreden/ir_constants.hpp>
#include <irreden/input/components/component_hitbox_2d_lua.hpp>
#include <irreden/input/pick_priority.hpp>
#include <irreden/render/ir_render_types.hpp>

namespace {

using IRPrefab::PickPriority::Candidate;

TEST(PickPriority, DetachedBoundsLosesToVoxelExactInFront) {
    Candidate best{41, 0, 24};
    IRPrefab::PickPriority::select(best, Candidate{73, 0, 8});
    EXPECT_EQ(best.entity_, 73);
}

TEST(PickPriority, NearestHitboxWinsOverlap) {
    Candidate best;
    IRPrefab::PickPriority::select(best, Candidate{41, 0, 24});
    IRPrefab::PickPriority::select(best, Candidate{73, 0, 8});
    EXPECT_EQ(best.entity_, 73);
}

TEST(PickPriority, HigherEntityPriorityWinsRegardlessOfDepth) {
    Candidate best{41, 0, -80};
    IRPrefab::PickPriority::select(best, Candidate{73, 1, 80});
    EXPECT_EQ(best.entity_, 73);
}

TEST(PickPriority, TrixelExactWinsWithoutHitbox) {
    Candidate best;
    IRPrefab::PickPriority::select(best, Candidate{73, 0, 8});
    EXPECT_EQ(best.entity_, 73);
}

TEST(PickPriority, EqualCandidatesKeepStableScanOrder) {
    Candidate best{41, 0, 8};
    IRPrefab::PickPriority::select(best, Candidate{73, 0, 8});
    EXPECT_EQ(best.entity_, 41);
}

TEST(CHitBox2DTest, DefaultPaddingIsGenerousAndLuaBound) {
    const IRComponents::C_HitBox2D hitbox;
    EXPECT_EQ(hitbox.padding_, IRConstants::kDefaultPickPadding);
    EXPECT_GT(hitbox.padding_, 0.0f);
    static_assert(IRScript::kHasLuaBinding<IRComponents::C_HitBox2D>);
}

TEST(PickPriority, NormalizedDepthUsesCompositeRange) {
    EXPECT_EQ(
        IRRender::compositeRawDepthFromNormalized(0.0f),
        static_cast<float>(IRConstants::kTrixelDistanceMinDistance)
    );
    EXPECT_EQ(
        IRRender::compositeRawDepthFromNormalized(1.0f),
        static_cast<float>(IRConstants::kTrixelDistanceMaxDistance)
    );
}

} // namespace
