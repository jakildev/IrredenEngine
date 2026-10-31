#include <irreden/render/sun_face_query_layout.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace {

using namespace IRPrefab::SunShadow;

TEST(SunRecordPoolLayoutTest, DefaultLayoutMatchesRetainedConstants) {
    constexpr SourceFaceLayout layout{kSourceFaceCapacity};
    static_assert(layout.spillCounterOffset() == kSourceFaceSpillCounterOffset);
    static_assert(layout.hierarchyOffset() == kSourceFaceHierarchyOffset);
    static_assert(layout.bufferWords() == kSourceFaceBufferWords);
    EXPECT_EQ(kSourceFaceTileOffset, kSourceFaceCapacityOffset + 1u);
    EXPECT_EQ(layout.bufferBytes(), kSourceFaceBufferWords * 4u);
}

TEST(SunRecordPoolLayoutTest, TailFollowsRecordPoolContiguously) {
    for (std::uint32_t capacity = kSourceFaceHierarchyLeafSize;
         capacity <= kSourceFaceCapacityDispatchLimit;
         capacity *= 2u) {
        const SourceFaceLayout layout{capacity};
        EXPECT_EQ(
            layout.spillCounterOffset(),
            kSourceFaceRecordOffset + capacity * kSourceFaceRecordWords
        );
        EXPECT_EQ(layout.spillHeadOffset(), layout.spillCounterOffset() + 1u);
        EXPECT_EQ(layout.spillNodeOffset(), layout.spillHeadOffset() + kSourceFaceTileCount);
        EXPECT_EQ(
            layout.hierarchyNeededOffset(),
            layout.spillNodeOffset() + kSourceFaceSpillCapacity * kSourceFaceSpillNodeWords
        );
        EXPECT_EQ(layout.hierarchyOffset(), layout.hierarchyNeededOffset() + 2u);
        EXPECT_EQ(layout.hierarchyMaxLeaves() * kSourceFaceHierarchyLeafSize, capacity);
        EXPECT_EQ(
            layout.bufferWords(),
            layout.hierarchyOffset() +
                2u * layout.hierarchyMaxLeaves() * kSourceFaceHierarchyNodeWords
        );
        // The GLSL twin recovers the capacity from the bound length alone.
        EXPECT_EQ(SourceFaceLayout::capacityForBufferWords(layout.bufferWords()), capacity);
    }
}

TEST(SunRecordPoolLayoutTest, BuildPassesCoverTheLeafAllocation) {
    const std::vector<std::pair<std::uint32_t, int>> expected{
        {8u, 2},
        {65536u, 2},
        {131072u, 2},
        {262144u, 3},
        {1048576u, 3},
        {kSourceFaceCapacityDispatchLimit, kSourceFaceHierarchyMaxBuildPasses}
    };
    for (const auto &[capacity, passes] : expected) {
        const SourceFaceLayout layout{capacity};
        EXPECT_EQ(layout.buildPasses(), passes) << capacity;
        EXPECT_LE(layout.buildPasses(), kSourceFaceHierarchyMaxBuildPasses);
        // Each pass roots one subtree per group; the last pass roots exactly one.
        std::uint32_t level = layout.hierarchyMaxLeaves();
        for (int pass = 0; pass < layout.buildPasses(); ++pass) {
            level = (level + kSourceFaceHierarchyWorkgroupSize - 1u) /
                    kSourceFaceHierarchyWorkgroupSize;
            EXPECT_EQ(layout.buildGroups(pass), level < 1u ? 1u : level) << capacity << ' ' << pass;
        }
        EXPECT_EQ(layout.buildGroups(layout.buildPasses() - 1), 1u) << capacity;
    }
    EXPECT_EQ(SourceFaceLayout{65536u}.buildGroups(0), 64u);
    EXPECT_EQ(SourceFaceLayout{65536u}.buildGroups(1), 1u);
}

TEST(SunRecordPoolLayoutTest, ValidCapacitiesArePowersOfTwoBelowTheLostBit) {
    EXPECT_TRUE(sourceFaceCapacityValid(8u));
    EXPECT_TRUE(sourceFaceCapacityValid(kSourceFaceCapacity));
    EXPECT_TRUE(sourceFaceCapacityValid(kSourceFaceCapacityCeiling));
    EXPECT_TRUE(sourceFaceCapacityValid(kSourceFaceCapacityDispatchLimit));
    EXPECT_FALSE(sourceFaceCapacityValid(2u * kSourceFaceCapacityDispatchLimit));
    EXPECT_FALSE(sourceFaceCapacityValid(0u));
    EXPECT_FALSE(sourceFaceCapacityValid(4u));
    EXPECT_FALSE(sourceFaceCapacityValid(12u));
    EXPECT_FALSE(sourceFaceCapacityValid(65536u + 8u));
    EXPECT_FALSE(sourceFaceCapacityValid(kSourceFaceTileRecordLost));
}

TEST(SunRecordPoolGrowthTest, DemandWithinThePoolKeepsTheCapacity) {
    EXPECT_EQ(grownSourceFaceCapacity(0u, 65536u, 1048576u), 65536u);
    EXPECT_EQ(grownSourceFaceCapacity(65535u, 65536u, 1048576u), 65536u);
    EXPECT_EQ(grownSourceFaceCapacity(65536u, 65536u, 1048576u), 65536u);
    // A sample completed under an older, smaller pool asks for nothing once the
    // pool already holds it.
    EXPECT_EQ(grownSourceFaceCapacity(71470u, 131072u, 1048576u), 131072u);
}

TEST(SunRecordPoolGrowthTest, DemandPastThePoolGrowsToTheSmallestHoldingPowerOfTwo) {
    EXPECT_EQ(grownSourceFaceCapacity(65537u, 65536u, 1048576u), 131072u);
    EXPECT_EQ(grownSourceFaceCapacity(71470u, 65536u, 1048576u), 131072u);
    EXPECT_EQ(grownSourceFaceCapacity(131072u, 65536u, 1048576u), 131072u);
    EXPECT_EQ(grownSourceFaceCapacity(131073u, 65536u, 1048576u), 262144u);
    EXPECT_EQ(grownSourceFaceCapacity(800000u, 65536u, 1048576u), 1048576u);
    for (std::uint32_t requested = 1u; requested <= 1048576u; requested = requested * 3u + 1u) {
        const auto grown = grownSourceFaceCapacity(requested, 8u, 1048576u);
        EXPECT_TRUE(sourceFaceCapacityValid(grown)) << requested;
        EXPECT_GE(grown, requested);
        EXPECT_TRUE(grown == 8u || grown / 2u < requested) << requested << ' ' << grown;
    }
}

TEST(SunRecordPoolGrowthTest, CeilingBoundsGrowthAndNeverShrinks) {
    EXPECT_EQ(grownSourceFaceCapacity(2000000u, 65536u, 1048576u), 1048576u);
    EXPECT_EQ(grownSourceFaceCapacity(2000000u, 1048576u, 1048576u), 1048576u);
    // A frozen pool (ceiling equal to the capacity) reports the exhaustion
    // through the unchanged capacity instead of reallocating.
    EXPECT_EQ(grownSourceFaceCapacity(71470u, 65536u, 65536u), 65536u);
    EXPECT_EQ(grownSourceFaceCapacity(10u, 131072u, 1048576u), 131072u);
}

} // namespace
