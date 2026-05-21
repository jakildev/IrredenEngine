#include <gtest/gtest.h>

#include <irreden/world/chunk_coord.hpp>
#include <irreden/world/chunk_residency.hpp>

#include <vector>

namespace {

using IRPrefab::Chunk::ChunkKey;
using IRPrefab::Chunk::pack;
using IRWorld::ChunkResidencyManager;
using IRWorld::ChunkResidencySlot;
using IRWorld::ChunkResidencyState;
using IRWorld::RequestPriority;

TEST(ChunkResidencyManagerTest, EmptyManagerHasNoChunksOrEntities) {
    ChunkResidencyManager mgr;
    EXPECT_EQ(mgr.residentChunkCount(), 0u);
    EXPECT_EQ(mgr.entityCount(), 0u);
    EXPECT_FALSE(mgr.isResident(pack(IRMath::ivec3{0, 0, 0})));
    EXPECT_EQ(mgr.slot(pack(IRMath::ivec3{0, 0, 0})), nullptr);
}

TEST(ChunkResidencyManagerTest, RequestResidentMakesSlotAndReachesResident) {
    ChunkResidencyManager mgr;
    auto key = pack(IRMath::ivec3{1, 2, 3});

    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);

    EXPECT_TRUE(mgr.isResident(key));
    EXPECT_EQ(mgr.residentChunkCount(), 1u);
    const auto *s = mgr.slot(key);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->key_, key);
    EXPECT_EQ(s->state_, ChunkResidencyState::RESIDENT);
    EXPECT_TRUE(s->ownedEntities_.empty());
}

TEST(ChunkResidencyManagerTest, RequestResidentIsIdempotent) {
    ChunkResidencyManager mgr;
    auto key = pack(IRMath::ivec3{5, 5, 5});
    mgr.requestResident(key, RequestPriority::PREFETCH_RING);
    mgr.requestResident(key, RequestPriority::PREFETCH_RING);
    EXPECT_EQ(mgr.residentChunkCount(), 1u);
}

TEST(ChunkResidencyManagerTest, RequestEvictRemovesSlot) {
    ChunkResidencyManager mgr;
    auto key = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(key, RequestPriority::FORCED);
    EXPECT_TRUE(mgr.isResident(key));

    mgr.requestEvict(key);
    EXPECT_FALSE(mgr.isResident(key));
    EXPECT_EQ(mgr.residentChunkCount(), 0u);
}

TEST(ChunkResidencyManagerTest, RequestEvictOnAbsentKeyIsNoop) {
    ChunkResidencyManager mgr;
    mgr.requestEvict(pack(IRMath::ivec3{42, 0, 0}));
    EXPECT_EQ(mgr.residentChunkCount(), 0u);
}

TEST(ChunkResidencyManagerTest, SpawnAcrossMultipleChunksAndIterateIndex) {
    // Acceptance criterion (1): "Spawn N entities across M chunks;
    // iterate the chunk index."
    ChunkResidencyManager mgr;
    struct Spawn {
        IREntity::EntityId id_;
        IRMath::ivec3 chunkCoord_;
    };
    std::vector<Spawn> spawns = {
        {100, IRMath::ivec3{0, 0, 0}},
        {101, IRMath::ivec3{0, 0, 0}},
        {102, IRMath::ivec3{1, 0, 0}},
        {103, IRMath::ivec3{0, 1, 0}},
        {104, IRMath::ivec3{-1, 0, 0}},
        {105, IRMath::ivec3{-1, 0, 0}},
        {106, IRMath::ivec3{0, 0, -1}},
    };
    for (const auto &s : spawns) {
        ChunkKey k = pack(s.chunkCoord_);
        mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
        mgr.attachEntity(s.id_, k);
    }

    EXPECT_EQ(mgr.residentChunkCount(), 5u);
    EXPECT_EQ(mgr.entityCount(), spawns.size());

    // Iterate the chunk index, summing per-chunk entity counts.
    std::size_t visited = 0;
    std::size_t totalEntities = 0;
    mgr.forEachChunk([&](ChunkKey, const ChunkResidencySlot &s) {
        ++visited;
        totalEntities += s.ownedEntities_.size();
    });
    EXPECT_EQ(visited, 5u);
    EXPECT_EQ(totalEntities, spawns.size());
}

TEST(ChunkResidencyManagerTest, AttachEntityIsIdempotentForSameId) {
    ChunkResidencyManager mgr;
    auto k = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    mgr.attachEntity(42, k);
    mgr.attachEntity(42, k);
    const auto *s = mgr.slot(k);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->ownedEntities_.size(), 1u);
}

TEST(ChunkResidencyManagerTest, AttachEntityToAbsentChunkIsNoop) {
    ChunkResidencyManager mgr;
    auto k = pack(IRMath::ivec3{9, 9, 9});
    mgr.attachEntity(7, k);
    EXPECT_EQ(mgr.residentChunkCount(), 0u);
    EXPECT_EQ(mgr.entityCount(), 0u);
}

TEST(ChunkResidencyManagerTest, MigrateEntityMovesOwnership) {
    ChunkResidencyManager mgr;
    auto src = pack(IRMath::ivec3{0, 0, 0});
    auto dst = pack(IRMath::ivec3{1, 0, 0});
    mgr.requestResident(src, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(dst, RequestPriority::VISIBLE_RENDER);
    mgr.attachEntity(11, src);

    mgr.migrateEntity(11, src, dst);

    const auto *srcSlot = mgr.slot(src);
    const auto *dstSlot = mgr.slot(dst);
    ASSERT_NE(srcSlot, nullptr);
    ASSERT_NE(dstSlot, nullptr);
    EXPECT_TRUE(srcSlot->ownedEntities_.empty());
    ASSERT_EQ(dstSlot->ownedEntities_.size(), 1u);
    EXPECT_EQ(dstSlot->ownedEntities_[0], IREntity::EntityId{11});
}

TEST(ChunkResidencyManagerTest, MigrateEntityForceResidesAbsentDestination) {
    ChunkResidencyManager mgr;
    auto src = pack(IRMath::ivec3{0, 0, 0});
    auto dst = pack(IRMath::ivec3{2, 2, 2});
    mgr.requestResident(src, RequestPriority::VISIBLE_RENDER);
    mgr.attachEntity(99, src);

    mgr.migrateEntity(99, src, dst);

    EXPECT_TRUE(mgr.isResident(dst));
    const auto *dstSlot = mgr.slot(dst);
    ASSERT_NE(dstSlot, nullptr);
    ASSERT_EQ(dstSlot->ownedEntities_.size(), 1u);
    EXPECT_EQ(dstSlot->ownedEntities_[0], IREntity::EntityId{99});
}

TEST(ChunkResidencyManagerTest, MigrateEntitySameKeyIsNoop) {
    ChunkResidencyManager mgr;
    auto k = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    mgr.attachEntity(33, k);
    mgr.migrateEntity(33, k, k);
    const auto *s = mgr.slot(k);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->ownedEntities_.size(), 1u);
}

TEST(ChunkResidencyManagerTest, BeginFrameAdvancesTouchFrame) {
    ChunkResidencyManager mgr;
    auto k = pack(IRMath::ivec3{0, 0, 0});

    mgr.beginFrame();
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    auto firstTouched = mgr.slot(k)->lastTouchedFrame_;

    mgr.beginFrame();
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    auto secondTouched = mgr.slot(k)->lastTouchedFrame_;

    EXPECT_LT(firstTouched, secondTouched);
}

TEST(ChunkResidencyManagerTest, PoolAllocatorReceivesPerChunkRequest) {
    // Acceptance criterion (2): "Per-chunk voxel sub-pool allocated
    // from the global pool." Verified via an injected fake allocator —
    // production wires this to IRRender::allocateVoxels.
    int callCount = 0;
    unsigned int lastRequestedSize = 0;
    ChunkResidencyManager::Config cfg;
    cfg.voxelsPerChunk_ = 4096;
    cfg.poolAllocator_ = [&](unsigned int size) {
        ++callCount;
        lastRequestedSize = size;
        IRRender::VoxelPoolAllocation alloc{};
        alloc.startIndex_ = static_cast<std::size_t>(callCount) * 4096u;
        return alloc;
    };
    ChunkResidencyManager mgr{std::move(cfg)};

    mgr.requestResident(pack(IRMath::ivec3{0, 0, 0}), RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(pack(IRMath::ivec3{1, 0, 0}), RequestPriority::VISIBLE_RENDER);

    EXPECT_EQ(callCount, 2);
    EXPECT_EQ(lastRequestedSize, 4096u);

    const auto *s0 = mgr.slot(pack(IRMath::ivec3{0, 0, 0}));
    const auto *s1 = mgr.slot(pack(IRMath::ivec3{1, 0, 0}));
    ASSERT_NE(s0, nullptr);
    ASSERT_NE(s1, nullptr);
    EXPECT_NE(s0->poolAllocation_.startIndex_, s1->poolAllocation_.startIndex_);

    // Re-requesting an already-resident chunk doesn't re-allocate.
    mgr.requestResident(pack(IRMath::ivec3{0, 0, 0}), RequestPriority::VISIBLE_RENDER);
    EXPECT_EQ(callCount, 2);
}

TEST(ChunkResidencyManagerTest, NoAllocatorMeansEmptyAllocationButStillResident) {
    ChunkResidencyManager mgr;
    auto k = pack(IRMath::ivec3{4, 4, 4});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    const auto *s = mgr.slot(k);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state_, ChunkResidencyState::RESIDENT);
    EXPECT_EQ(s->poolAllocation_.startIndex_, 0u);
    EXPECT_TRUE(s->poolAllocation_.voxels_.empty());
}

} // namespace
