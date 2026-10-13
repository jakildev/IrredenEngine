#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

namespace {

using IRComponents::C_VoxelPool;
using IRMath::ivec3;

TEST(VoxelPoolCapacity, MatchesAllocationForTailExactFreeSpanLargerSpanAndExhaustion) {
    C_VoxelPool pool{ivec3(2, 2, 2)};

    EXPECT_TRUE(pool.canAllocateVoxels(3));
    const auto tail = pool.allocateVoxels(3);
    EXPECT_EQ(tail.voxels_.size(), 3u);

    const auto exact = pool.allocateVoxels(2);
    pool.deallocateVoxels(exact.startIndex_, 2);
    EXPECT_TRUE(pool.canAllocateVoxels(2));
    EXPECT_EQ(pool.allocateVoxels(2).startIndex_, exact.startIndex_);

    const auto larger = pool.allocateVoxels(3);
    pool.deallocateVoxels(larger.startIndex_, 3);
    EXPECT_FALSE(pool.canAllocateVoxels(2));

    EXPECT_FALSE(pool.canAllocateVoxels(1));
}

} // namespace
