#include <gtest/gtest.h>

#include <irreden/render/fog_of_war.hpp>
#include "common/allocation_counter.hpp"

#include <vector>

namespace {

using IRComponents::C_FogRevealed;
using IRComponents::C_WorldTransform;
using IRComponents::FogHiddenPolicy;
using IRComponents::FogOverride;

TEST(FogGhostPolicyTest, CapturesHoldsRefreshesAndDiscards) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    fog.shown_ = true;
    C_WorldTransform first{};
    first.translation_ = {1.0f, 2.0f, 3.0f};
    IRPrefab::Fog::stepGhostLifecycle(fog, first, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostPoseValid_);
    EXPECT_EQ(fog.ghostPose_.translation_, first.translation_);

    fog.shown_ = false;
    C_WorldTransform moved{};
    moved.translation_ = {9.0f, 8.0f, 7.0f};
    IRPrefab::Fog::stepGhostLifecycle(fog, moved, true, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostHeld_);
    EXPECT_EQ(fog.ghostPose_.translation_, first.translation_);

    IRPrefab::Fog::stepGhostLifecycle(fog, moved, false, true, 0.75f, [](auto, auto) {
        return 1.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, PolicyAndOverridesCannotCreateGhostsFromInvalidPose) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    C_WorldTransform pose{};
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, true, false, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);

    fog.ghostHeld_ = true;
    fog.ghostPoseValid_ = true;
    fog.override_ = FogOverride::FORCE_HIDDEN;
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);

    fog.override_ = FogOverride::NONE;
    fog.ghostHeld_ = true;
    fog.ghostPoseValid_ = true;
    fog.hiddenPolicy_ = FogHiddenPolicy::HIDE;
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, false, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, WarmHeldLifecycleIsAllocationFreeAndLinear) {
    std::vector<C_FogRevealed> fogs(262144);
    C_WorldTransform pose{};
    for (auto &fog : fogs) {
        fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
        fog.ghostHeld_ = true;
        fog.ghostPoseValid_ = true;
    }
    auto run = [&]() {
        for (auto &fog : fogs) {
            IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, true, 0.75f, [](auto, auto) {
                return 0.0f;
            });
        }
    };
    run();
    IRTest::AllocationCounter allocations;
    run();
    EXPECT_EQ(allocations.allocations(), 0u);
}

} // namespace
