#include <gtest/gtest.h>

#include <irreden/render/voxel_pool_config.hpp>

#include <cstdint>
#include <limits>

// Range contract for IRRender::VoxelPoolConfig::setSize: the edge is clamped
// to [1, kMaxEdge] in every build config, so getTotalSize never overflows int
// and the per-axis overflow lane's face demand stays inside its 2^30 field.

namespace {

namespace VoxelPoolConfig = IRRender::VoxelPoolConfig;

// setSize mutates process-global state other tests read; restore it.
class VoxelPoolConfigRange : public ::testing::Test {
  protected:
    void SetUp() override {
        m_savedEdge = VoxelPoolConfig::getEdge();
    }
    void TearDown() override {
        VoxelPoolConfig::setSize(m_savedEdge);
    }

  private:
    int m_savedEdge = VoxelPoolConfig::kDefaultEdge;
};

TEST_F(VoxelPoolConfigRange, MaxEdgeIsAccepted) {
    VoxelPoolConfig::setSize(VoxelPoolConfig::kMaxEdge);
    EXPECT_EQ(VoxelPoolConfig::getEdge(), VoxelPoolConfig::kMaxEdge);
    EXPECT_EQ(
        VoxelPoolConfig::getTotalSize(),
        VoxelPoolConfig::kMaxEdge * VoxelPoolConfig::kMaxEdge * VoxelPoolConfig::kMaxEdge
    );
}

// Positive control: the first edge past the bound is the first one clamped.
TEST_F(VoxelPoolConfigRange, FirstRejectedEdgeClampsToMax) {
    VoxelPoolConfig::setSize(VoxelPoolConfig::kMaxEdge + 1);
    EXPECT_EQ(VoxelPoolConfig::getEdge(), VoxelPoolConfig::kMaxEdge);
}

// Edges whose cube would overflow int (>= 1291) clamp rather than wrap.
TEST_F(VoxelPoolConfigRange, IntOverflowingEdgesClampToMax) {
    for (const int edge : {1291, 4096, std::numeric_limits<int>::max()}) {
        VoxelPoolConfig::setSize(edge);
        EXPECT_EQ(VoxelPoolConfig::getEdge(), VoxelPoolConfig::kMaxEdge) << "edge " << edge;
        EXPECT_GT(VoxelPoolConfig::getTotalSize(), 0) << "edge " << edge;
    }
}

TEST_F(VoxelPoolConfigRange, NonPositiveEdgesClampToOne) {
    for (const int edge : {0, -1, std::numeric_limits<int>::min()}) {
        VoxelPoolConfig::setSize(edge);
        EXPECT_EQ(VoxelPoolConfig::getEdge(), 1) << "edge " << edge;
    }
}

// The per-axis overflow lane's face demand at the largest pool stays within
// the signed 2^30 field overflowCapacityFor asserts on.
TEST_F(VoxelPoolConfigRange, MaxPoolFaceDemandFitsOverflowField) {
    VoxelPoolConfig::setSize(std::numeric_limits<int>::max());
    const std::uint64_t faceDemand =
        static_cast<std::uint64_t>(VoxelPoolConfig::getTotalSize()) * 3u;
    EXPECT_LE(faceDemand, std::uint64_t{1} << 30);
}

} // namespace
