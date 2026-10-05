#include <gtest/gtest.h>

#include <irreden/render/light_volume_dispatch.hpp>

#include <array>
#include <span>
#include <stdexcept>

namespace {

using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;
using IRRender::GPULightSource;
using IRRender::detail::lightVolumePropagationDispatch;

GPULightSource stagedSeed(ivec3 cell, ivec3 volumeOrigin = ivec3(0)) {
    GPULightSource light{};
    light.originAndType_ = vec4(vec3(volumeOrigin + cell - ivec3(64)), 0.0f);
    light.trueOriginVoxel_ = light.originAndType_;
    light.colorAndIntensity_ = vec4(1.0f);
    light.coneAndSeedAlpha_.y = 1.0f;
    return light;
}

TEST(LightVolumeDispatchTest, EmptyStagingEmitsNoWork) {
    const auto dispatch =
        lightVolumePropagationDispatch(std::span<const GPULightSource>{}, ivec3(-100), 24);
    EXPECT_EQ(dispatch.origin_, ivec3(0));
    EXPECT_EQ(dispatch.groups_, ivec3(0));
}

TEST(LightVolumeDispatchTest, MisalignedSeedUsesTheSmallestAlignedBox) {
    const std::array lights{stagedSeed(ivec3(13, 19, 7))};
    const auto dispatch = lightVolumePropagationDispatch(lights, ivec3(0), 1);
    EXPECT_EQ(dispatch.origin_, ivec3(8, 16, 4));
    EXPECT_EQ(dispatch.groups_, ivec3(1, 1, 2));
}

TEST(LightVolumeDispatchTest, ReachOnAGroupBoundaryIncludesTheLastCell) {
    const std::array lights{stagedSeed(ivec3(7, 7, 3))};
    const auto dispatch = lightVolumePropagationDispatch(lights, ivec3(0), 1);
    EXPECT_EQ(dispatch.origin_, ivec3(0));
    EXPECT_EQ(dispatch.groups_, ivec3(2));
}

TEST(LightVolumeDispatchTest, ShiftedBoundarySeedsUseTheirStagedPositionNotTheApex) {
    const ivec3 volumeOrigin(-100, 200, -300);
    std::array lights{stagedSeed(ivec3(0, 71, 127), volumeOrigin)};
    lights[0].trueOriginVoxel_ = vec4(-1000.0f, 2000.0f, -3000.0f, 0.0f);
    lights[0].coneAndSeedAlpha_.y = 0.125f;
    const auto clamped = lightVolumePropagationDispatch(lights, volumeOrigin, 3);
    EXPECT_EQ(clamped.origin_, ivec3(0, 64, 124));
    EXPECT_EQ(clamped.groups_, ivec3(1, 2, 1));

    // A boundary relocation changes the staged texel while preserving the light apex.
    lights[0].originAndType_ = stagedSeed(ivec3(0, 74, 124), volumeOrigin).originAndType_;
    const auto relocated = lightVolumePropagationDispatch(lights, volumeOrigin, 3);
    EXPECT_EQ(relocated.origin_, ivec3(0, 64, 120));
    EXPECT_EQ(relocated.groups_, ivec3(1, 2, 2));
}

TEST(LightVolumeDispatchTest, BlackAndSubquantizedSeedsRetainTheFullIterationReach) {
    std::array lights{stagedSeed(ivec3(21, 42, 63))};
    lights[0].directionAndRadius_.w = 1.0f;
    for (float color : {0.0f, 1.0f}) {
        for (float alpha : {0.0f, 0.001f}) {
            lights[0].colorAndIntensity_ = vec4(color);
            lights[0].coneAndSeedAlpha_.y = alpha;
            const auto dispatch = lightVolumePropagationDispatch(lights, ivec3(0), 24);
            EXPECT_EQ(dispatch.origin_, ivec3(0, 16, 36));
            EXPECT_EQ(dispatch.groups_, ivec3(6, 7, 13));
        }
    }
}

TEST(LightVolumeDispatchTest, SeparatedCornerSeedsCoverTheFullDomain) {
    const std::array lights{stagedSeed(ivec3(0)), stagedSeed(ivec3(127))};
    for (int iterations : {1, 3, 24, 32}) {
        const auto dispatch = lightVolumePropagationDispatch(lights, ivec3(0), iterations);
        EXPECT_EQ(dispatch.origin_, ivec3(0));
        EXPECT_EQ(dispatch.groups_, ivec3(16, 16, 32));
    }
}

TEST(LightVolumeDispatchTest, ContainsEveryCellReachableByTheSixNeighborStencil) {
    const ivec3 volumeOrigin(-317, 219, -101);
    for (const ivec3 seed : {ivec3(0), ivec3(127), ivec3(65, 59, 61), ivec3(3, 77, 124)}) {
        const std::array lights{stagedSeed(seed, volumeOrigin)};
        for (int iterations : {1, 3, 24, 32}) {
            SCOPED_TRACE(
                testing::Message() << "seed " << seed.x << "," << seed.y << "," << seed.z
                                   << " iterations " << iterations
            );
            const auto dispatch = lightVolumePropagationDispatch(lights, volumeOrigin, iterations);
            const ivec3 end = dispatch.origin_ + dispatch.groups_ * ivec3(8, 8, 4);
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_GE(dispatch.origin_[axis], 0);
                EXPECT_LE(end[axis], 128);
                EXPECT_GT(dispatch.groups_[axis], 0);
                EXPECT_EQ(dispatch.origin_[axis] % ivec3(8, 8, 4)[axis], 0);
            }
            int reachable = 0;
            int omitted = 0;
            for (int z = -iterations; z <= iterations; ++z) {
                for (int y = -iterations; y <= iterations; ++y) {
                    for (int x = -iterations; x <= iterations; ++x) {
                        if (IRMath::abs(x) + IRMath::abs(y) + IRMath::abs(z) > iterations) {
                            continue;
                        }
                        const ivec3 cell = seed + ivec3(x, y, z);
                        if (cell.x < 0 || cell.y < 0 || cell.z < 0 || cell.x >= 128 ||
                            cell.y >= 128 || cell.z >= 128) {
                            continue;
                        }
                        ++reachable;
                        if (cell.x < dispatch.origin_.x || cell.y < dispatch.origin_.y ||
                            cell.z < dispatch.origin_.z || cell.x >= end.x || cell.y >= end.y ||
                            cell.z >= end.z) {
                            ++omitted;
                        }
                    }
                }
            }
            EXPECT_GT(reachable, 0);
            EXPECT_EQ(omitted, 0);
        }
    }
}

#ifndef IR_RELEASE
TEST(LightVolumeDispatchTest, RejectsNonpositiveIterationCounts) {
    const std::array lights{stagedSeed(ivec3(64))};
    EXPECT_THROW(lightVolumePropagationDispatch(lights, ivec3(0), 0), std::runtime_error);
    EXPECT_THROW(lightVolumePropagationDispatch(lights, ivec3(0), -1), std::runtime_error);
}
#endif

} // namespace
