#include <gtest/gtest.h>

#include <irreden/world/chunk_coord.hpp>
#include <irreden/ir_constants.hpp>

namespace {

using IRPrefab::Chunk::chunkCenterWorld;
using IRPrefab::Chunk::ChunkKey;
using IRPrefab::Chunk::chunkOriginVoxel;
using IRPrefab::Chunk::pack;
using IRPrefab::Chunk::unpack;
using IRPrefab::Chunk::worldToChunk;

constexpr int kEdge = static_cast<int>(IRConstants::kChunkSize.x);

TEST(ChunkCoordTest, WorldToChunkZero) {
    EXPECT_EQ(worldToChunk(IRMath::ivec3{0, 0, 0}), IRMath::ivec3(0, 0, 0));
}

TEST(ChunkCoordTest, WorldToChunkPositiveInteriorAndBoundary) {
    EXPECT_EQ(worldToChunk(IRMath::ivec3{kEdge - 1, 0, 0}), IRMath::ivec3(0, 0, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{kEdge, 0, 0}), IRMath::ivec3(1, 0, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{0, 2 * kEdge - 1, 0}), IRMath::ivec3(0, 1, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{0, 0, 3 * kEdge + 5}), IRMath::ivec3(0, 0, 3));
}

TEST(ChunkCoordTest, WorldToChunkFloorsNegativeTowardMinusInfinity) {
    // -1 must land in chunk -1, not chunk 0 — C++ integer division
    // would truncate toward zero. This is the bug the floor-divide
    // helper exists to prevent.
    EXPECT_EQ(worldToChunk(IRMath::ivec3{-1, 0, 0}), IRMath::ivec3(-1, 0, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{-kEdge, 0, 0}), IRMath::ivec3(-1, 0, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{-kEdge - 1, 0, 0}), IRMath::ivec3(-2, 0, 0));
    EXPECT_EQ(worldToChunk(IRMath::ivec3{0, -2 * kEdge, 0}), IRMath::ivec3(0, -2, 0));
}

TEST(ChunkCoordTest, ChunkOriginVoxelLinear) {
    EXPECT_EQ(chunkOriginVoxel(IRMath::ivec3(0, 0, 0)), IRMath::ivec3(0, 0, 0));
    EXPECT_EQ(chunkOriginVoxel(IRMath::ivec3(1, 0, 0)), IRMath::ivec3(kEdge, 0, 0));
    EXPECT_EQ(
        chunkOriginVoxel(IRMath::ivec3(-1, 2, -3)),
        IRMath::ivec3(-kEdge, 2 * kEdge, -3 * kEdge)
    );
}

TEST(ChunkCoordTest, ChunkCenterWorldHalfWayBetweenCorners) {
    auto center = chunkCenterWorld(IRMath::ivec3(0, 0, 0));
    EXPECT_FLOAT_EQ(center.x, kEdge * 0.5f);
    EXPECT_FLOAT_EQ(center.y, kEdge * 0.5f);
    EXPECT_FLOAT_EQ(center.z, kEdge * 0.5f);

    auto centerNeg = chunkCenterWorld(IRMath::ivec3(-1, 0, 0));
    EXPECT_FLOAT_EQ(centerNeg.x, -kEdge * 0.5f);
}

TEST(ChunkCoordTest, OriginAndWorldToChunkRoundTrip) {
    for (int dx = -3; dx <= 3; ++dx) {
        for (int dy = -3; dy <= 3; ++dy) {
            for (int dz = -3; dz <= 3; ++dz) {
                IRMath::ivec3 c{dx, dy, dz};
                EXPECT_EQ(worldToChunk(chunkOriginVoxel(c)), c);
            }
        }
    }
}

TEST(ChunkCoordTest, PackUnpackRoundTripPositive) {
    for (int dx = 0; dx < 4; ++dx) {
        for (int dy = 0; dy < 4; ++dy) {
            for (int dz = 0; dz < 4; ++dz) {
                IRMath::ivec3 c{dx, dy, dz};
                ChunkKey k = pack(c);
                EXPECT_EQ(unpack(k), c);
            }
        }
    }
}

TEST(ChunkCoordTest, PackUnpackRoundTripNegativeAndMixed) {
    IRMath::ivec3 cases[] = {
        IRMath::ivec3{-1, 0, 0},
        IRMath::ivec3{0, -1, 0},
        IRMath::ivec3{0, 0, -1},
        IRMath::ivec3{-100, 200, -300},
        IRMath::ivec3{32767, -32768, 12345},
    };
    for (auto c : cases) {
        EXPECT_EQ(unpack(pack(c)), c) << "c=(" << c.x << ',' << c.y << ',' << c.z << ')';
    }
}

TEST(ChunkCoordTest, PackHighBitsAreReserved) {
    // No coord should ever set the top 16 bits.
    IRMath::ivec3 cases[] = {
        IRMath::ivec3{32767, 32767, 32767},
        IRMath::ivec3{-32768, -32768, -32768},
    };
    for (auto c : cases) {
        ChunkKey k = pack(c);
        EXPECT_EQ(k & 0xFFFF000000000000ULL, 0u);
    }
}

TEST(ChunkCoordTest, DistinctCoordsHaveDistinctKeys) {
    // Smoke check the int16 packing — y vs z should not alias.
    EXPECT_NE(pack(IRMath::ivec3{1, 0, 0}), pack(IRMath::ivec3{0, 1, 0}));
    EXPECT_NE(pack(IRMath::ivec3{0, 1, 0}), pack(IRMath::ivec3{0, 0, 1}));
    EXPECT_NE(pack(IRMath::ivec3{-1, 0, 0}), pack(IRMath::ivec3{0, 0, -1}));
}

} // namespace
