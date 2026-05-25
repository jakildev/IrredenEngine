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
    IRMath::vec3 cam{16.0f, 16.0f, 16.0f};

    mgr.beginFrame(cam);
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    auto firstTouched = mgr.slot(k)->lastTouchedFrame_;

    mgr.beginFrame(cam);
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

// ── E2: eviction policy tests ───────────────────────────────────────

ChunkResidencyManager::Config makeBudgetConfig(unsigned int maxChunks) {
    ChunkResidencyManager::Config cfg;
    cfg.maxResidentChunks_ = maxChunks;
    cfg.viewRadiusVoxels_ = 128.0f;
    cfg.prefetchRadiusVoxels_ = 256.0f;
    cfg.hysteresisVoxels_ = 32.0f;
    return cfg;
}

TEST(ChunkResidencyEviction, DistanceBasedEvictionMarksEvictingBeyondThreshold) {
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto near = pack(IRMath::ivec3{0, 0, 0});
    auto far = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(near, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(far, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});

    const auto *nearSlot = mgr.slot(near);
    const auto *farSlot = mgr.slot(far);
    ASSERT_NE(nearSlot, nullptr);
    ASSERT_NE(farSlot, nullptr);
    EXPECT_EQ(nearSlot->state_, ChunkResidencyState::RESIDENT);
    EXPECT_EQ(farSlot->state_, ChunkResidencyState::EVICTING);
}

TEST(ChunkResidencyEviction, EndFrameProcessesEvictingSlots) {
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto near = pack(IRMath::ivec3{0, 0, 0});
    auto far = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(near, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(far, RequestPriority::VISIBLE_RENDER);
    EXPECT_EQ(mgr.residentChunkCount(), 2u);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    mgr.endFrame();

    EXPECT_TRUE(mgr.isResident(near));
    EXPECT_FALSE(mgr.isResident(far));
    EXPECT_EQ(mgr.residentChunkCount(), 1u);
    EXPECT_EQ(mgr.frameStats().evictedThisFrame_, 1u);
}

TEST(ChunkResidencyEviction, BudgetCapEvictsFurthestFirst) {
    auto cfg = makeBudgetConfig(2);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto c0 = pack(IRMath::ivec3{0, 0, 0});
    auto c1 = pack(IRMath::ivec3{1, 0, 0});
    auto c2 = pack(IRMath::ivec3{2, 0, 0});
    auto c3 = pack(IRMath::ivec3{3, 0, 0});
    mgr.requestResident(c0, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(c1, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(c2, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(c3, RequestPriority::VISIBLE_RENDER);
    EXPECT_EQ(mgr.residentChunkCount(), 4u);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    mgr.endFrame();

    EXPECT_EQ(mgr.residentChunkCount(), 2u);
    EXPECT_TRUE(mgr.isResident(c0));
    EXPECT_TRUE(mgr.isResident(c1));
    EXPECT_FALSE(mgr.isResident(c2));
    EXPECT_FALSE(mgr.isResident(c3));
}

TEST(ChunkResidencyEviction, LRUTieBreakingEvictsOldestTouchedFirst) {
    auto cfg = makeBudgetConfig(2);
    // Small view radius so beginFrame doesn't bump lastTouchedFrame_
    // on any of the test chunks (all at ~177 voxels from camera).
    cfg.viewRadiusVoxels_ = 10.0f;
    cfg.prefetchRadiusVoxels_ = 20000.0f;
    ChunkResidencyManager mgr{std::move(cfg)};

    // Three chunks equidistant from the origin: symmetric along x/y/z
    // axes. Each center is at (176,16,16) / (16,176,16) / (16,16,176),
    // all at sqrt(176²+16²+16²) ≈ 177.4 from (0,0,0).
    auto c0 = pack(IRMath::ivec3{5, 0, 0});
    auto c1 = pack(IRMath::ivec3{0, 5, 0});
    auto c2 = pack(IRMath::ivec3{0, 0, 5});

    IRMath::vec3 cam{0.0f, 0.0f, 0.0f};
    mgr.beginFrame(cam);
    mgr.requestResident(c0, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(cam);
    mgr.requestResident(c1, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(cam);
    mgr.requestResident(c2, RequestPriority::VISIBLE_RENDER);

    mgr.endFrame();

    EXPECT_EQ(mgr.residentChunkCount(), 2u);
    EXPECT_FALSE(mgr.isResident(c0)) << "c0 should be evicted (oldest lastTouchedFrame_)";
    EXPECT_TRUE(mgr.isResident(c1));
    EXPECT_TRUE(mgr.isResident(c2));
}

TEST(ChunkResidencyEviction, HysteresisPreventsThrashing) {
    auto cfg = makeBudgetConfig(256);
    cfg.viewRadiusVoxels_ = 128.0f;
    cfg.prefetchRadiusVoxels_ = 256.0f;
    cfg.hysteresisVoxels_ = 32.0f;
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{8, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);

    float chunkCenterX = 8.0f * 32.0f + 16.0f;
    float justInsideEvictBoundary = chunkCenterX - 256.0f - 31.0f;
    mgr.beginFrame(IRMath::vec3{justInsideEvictBoundary, 16.0f, 16.0f});

    const auto *s = mgr.slot(k);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->state_, ChunkResidencyState::RESIDENT)
        << "Chunk inside R_prefetch + R_hysteresis should NOT be marked EVICTING";
}

TEST(ChunkResidencyEviction, DeallocatorCalledOnEviction) {
    int deallocCount = 0;
    auto cfg = makeBudgetConfig(256);
    cfg.poolAllocator_ = [](unsigned int) {
        IRRender::VoxelPoolAllocation alloc{};
        static std::vector<IRComponents::C_Voxel> storage(32);
        alloc.voxels_ = std::span<IRComponents::C_Voxel>{storage};
        alloc.startIndex_ = 1;
        return alloc;
    };
    cfg.poolDeallocator_ = [&](const IRRender::VoxelPoolAllocation &alloc) {
        ++deallocCount;
        EXPECT_FALSE(alloc.voxels_.empty());
    };
    cfg.voxelsPerChunk_ = 32;
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    EXPECT_TRUE(mgr.isResident(k));

    mgr.beginFrame(IRMath::vec3{0.0f, 0.0f, 0.0f});
    mgr.endFrame();

    EXPECT_EQ(deallocCount, 1)
        << "Pool deallocator should be called once when evicting a chunk with a pool allocation";
}

TEST(ChunkResidencyEviction, DeallocatorNotCalledForEmptyAllocation) {
    int deallocCount = 0;
    auto cfg = makeBudgetConfig(256);
    cfg.poolDeallocator_ = [&](const IRRender::VoxelPoolAllocation &) { ++deallocCount; };
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{0.0f, 0.0f, 0.0f});
    mgr.endFrame();

    EXPECT_EQ(deallocCount, 0)
        << "Pool deallocator should not be called when the allocation is empty";
}

TEST(ChunkResidencyEviction, FrameStatsReportCorrectCounts) {
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto near = pack(IRMath::ivec3{0, 0, 0});
    auto far = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(near, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(far, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    mgr.endFrame();

    EXPECT_EQ(mgr.frameStats().loadedThisFrame_, 0u);
    EXPECT_EQ(mgr.frameStats().evictedThisFrame_, 1u);
    EXPECT_EQ(mgr.frameStats().residentCount_, 1u);
}

TEST(ChunkResidencyEviction, CameraMovementTriggersEvictionCycle) {
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto c0 = pack(IRMath::ivec3{0, 0, 0});
    auto c1 = pack(IRMath::ivec3{20, 0, 0});
    mgr.requestResident(c0, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(c1, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    mgr.endFrame();
    EXPECT_TRUE(mgr.isResident(c0));
    EXPECT_FALSE(mgr.isResident(c1)) << "c1 is far from camera at origin, should be evicted";

    mgr.requestResident(c1, RequestPriority::VISIBLE_RENDER);
    float c1CenterX = 20.0f * 32.0f + 16.0f;
    mgr.beginFrame(IRMath::vec3{c1CenterX, 16.0f, 16.0f});
    mgr.endFrame();
    EXPECT_TRUE(mgr.isResident(c1)) << "c1 should remain resident when camera moves near it";
    EXPECT_FALSE(mgr.isResident(c0)) << "c0 is now far from camera, should be evicted";
}

TEST(ChunkResidencyEviction, ViewRadiusSlotsTouched) {
    auto cfg = makeBudgetConfig(256);
    cfg.viewRadiusVoxels_ = 128.0f;
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    auto touchedFrame = mgr.slot(k)->lastTouchedFrame_;
    EXPECT_GT(touchedFrame, 0u) << "Chunk within view radius should have its touch frame updated";
}

TEST(ChunkResidencyEviction, RequestResidentOnEvictingSlotRescuesToResident) {
    // Verifies Site-1 fix: requestResident called on an EVICTING slot must
    // clear the EVICTING state so endFrame does NOT erase it.
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{0.0f, 0.0f, 0.0f});
    ASSERT_EQ(mgr.slot(k)->state_, ChunkResidencyState::EVICTING)
        << "precondition: chunk must be marked EVICTING before the rescue call";

    mgr.requestResident(k, RequestPriority::FORCED);
    EXPECT_EQ(mgr.slot(k)->state_, ChunkResidencyState::RESIDENT)
        << "requestResident on EVICTING slot must rescue it to RESIDENT";

    mgr.endFrame();
    EXPECT_TRUE(mgr.isResident(k)) << "rescued slot must survive the next endFrame";
    EXPECT_EQ(mgr.frameStats().evictedThisFrame_, 0u);
}

TEST(ChunkResidencyEviction, MigrateEntityToEvictingDestinationRescuesSlot) {
    // Verifies Site-1 + Site-2 fix together: migrateEntity to an EVICTING
    // destination must rescue that slot so the entity appears after endFrame.
    // This test is the one Opus identified as proving the incomplete-fix gap
    // is closed — it fails if only the requestResident rescue is patched but
    // the migrateEntity guard is not changed to !isResident().
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto src = pack(IRMath::ivec3{0, 0, 0});
    auto dst = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(src, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(dst, RequestPriority::VISIBLE_RENDER);
    mgr.attachEntity(77, src);

    // Move camera near src, leaving dst far → dst becomes EVICTING.
    mgr.beginFrame(IRMath::vec3{16.0f, 16.0f, 16.0f});
    ASSERT_EQ(mgr.slot(dst)->state_, ChunkResidencyState::EVICTING)
        << "precondition: dst must be EVICTING before the migration";

    // Migrate entity to the EVICTING destination — must rescue dst.
    mgr.migrateEntity(77, src, dst);

    mgr.endFrame();
    EXPECT_TRUE(mgr.isResident(dst)) << "migrateEntity must have rescued the EVICTING dst slot";
    const auto *dstSlot = mgr.slot(dst);
    ASSERT_NE(dstSlot, nullptr);
    ASSERT_EQ(dstSlot->ownedEntities_.size(), 1u)
        << "entity must appear in dst slot after endFrame";
    EXPECT_EQ(dstSlot->ownedEntities_[0], IREntity::EntityId{77});
}

TEST(ChunkResidencyEviction, AttachEntityToEvictingSlotRescuesSlot) {
    // Verifies Site-3 fix: attachEntity on an EVICTING slot rescues it so
    // the entity is not silently lost when endFrame processes evictions.
    auto cfg = makeBudgetConfig(256);
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{100, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);

    mgr.beginFrame(IRMath::vec3{0.0f, 0.0f, 0.0f});
    ASSERT_EQ(mgr.slot(k)->state_, ChunkResidencyState::EVICTING)
        << "precondition: slot must be EVICTING before the attach call";

    mgr.attachEntity(88, k);
    EXPECT_EQ(mgr.slot(k)->state_, ChunkResidencyState::RESIDENT)
        << "attachEntity on EVICTING slot must rescue it to RESIDENT";

    mgr.endFrame();
    EXPECT_TRUE(mgr.isResident(k)) << "rescued slot must survive endFrame";
    const auto *s = mgr.slot(k);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->ownedEntities_.size(), 1u);
    EXPECT_EQ(s->ownedEntities_[0], IREntity::EntityId{88});
}

TEST(ChunkResidencyEviction, RequestEvictCallsDeallocator) {
    int deallocCount = 0;
    auto cfg = makeBudgetConfig(256);
    cfg.poolAllocator_ = [](unsigned int) {
        IRRender::VoxelPoolAllocation alloc{};
        static std::vector<IRComponents::C_Voxel> storage(32);
        alloc.voxels_ = std::span<IRComponents::C_Voxel>{storage};
        alloc.startIndex_ = 1;
        return alloc;
    };
    cfg.poolDeallocator_ = [&](const IRRender::VoxelPoolAllocation &) { ++deallocCount; };
    cfg.voxelsPerChunk_ = 32;
    ChunkResidencyManager mgr{std::move(cfg)};

    auto k = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(k, RequestPriority::VISIBLE_RENDER);
    mgr.requestEvict(k);

    EXPECT_EQ(deallocCount, 1);
}

// ---------------------------------------------------------------------------
// Prefetch ring (E3)
// ---------------------------------------------------------------------------

TEST(ChunkPrefetchTest, TickPrefetchRequestsRingAroundCamera) {
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    mgr.beginFrame(IRMath::vec3(16.0f, 16.0f, 16.0f));
    mgr.tickPrefetch();

    // Radius 1 → 3×3×3 = 27 chunks
    EXPECT_EQ(mgr.residentChunkCount(), 27u);
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{0, 0, 0})));
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{1, 1, 1})));
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{-1, -1, -1})));
}

TEST(ChunkPrefetchTest, TickPrefetchSetsDistanceOnSlots) {
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    mgr.beginFrame(IRMath::vec3(16.0f, 16.0f, 16.0f));
    mgr.tickPrefetch();

    const auto *center = mgr.slot(pack(IRMath::ivec3{0, 0, 0}));
    const auto *neighbor = mgr.slot(pack(IRMath::ivec3{1, 0, 0}));
    ASSERT_NE(center, nullptr);
    ASSERT_NE(neighbor, nullptr);
    EXPECT_LT(center->distanceVoxels_, neighbor->distanceVoxels_);
}

TEST(ChunkPrefetchTest, EvictsChunksOutsideEvictionRadius) {
    // Eviction is driven by beginFrame (Euclidean + hysteresis) + endFrame.
    // Default threshold: prefetchRadiusVoxels_(256) + hysteresisVoxels_(32) = 288.
    // Chunk (10,10,10) center is at ~554 voxels from camera at (16,16,16),
    // safely beyond 288 → marked EVICTING by beginFrame, removed by endFrame.
    // Ring chunks (radius 1, max ~55 voxels) are well inside the threshold.
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    IRMath::vec3 cam(16.0f, 16.0f, 16.0f);

    // Tick 1: populate ring
    mgr.beginFrame(cam);
    mgr.tickPrefetch();
    mgr.endFrame();
    EXPECT_EQ(mgr.residentChunkCount(), 27u);

    // Force a chunk far beyond the eviction threshold into the resident set
    auto farKey = pack(IRMath::ivec3{10, 10, 10});
    mgr.requestResident(farKey, RequestPriority::FORCED);
    EXPECT_TRUE(mgr.isResident(farKey));
    EXPECT_EQ(mgr.residentChunkCount(), 28u);

    // Tick 2: beginFrame marks farKey EVICTING, endFrame removes it
    mgr.beginFrame(cam);
    mgr.tickPrefetch();
    mgr.endFrame();
    EXPECT_FALSE(mgr.isResident(farKey));
    EXPECT_EQ(mgr.residentChunkCount(), 27u);
}

TEST(ChunkPrefetchTest, CameraWarpRelocatesResidentSet) {
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    // Tick 1: populate ring around chunk (0,0,0)
    mgr.beginFrame(IRMath::vec3(16.0f, 16.0f, 16.0f));
    mgr.tickPrefetch();
    mgr.endFrame();
    auto originKey = pack(IRMath::ivec3{0, 0, 0});
    EXPECT_TRUE(mgr.isResident(originKey));

    // Tick 2: warp far away — beginFrame marks origin ring as EVICTING,
    // tickPrefetch loads ring around chunk(100,100,100), endFrame removes old ring
    mgr.beginFrame(IRMath::vec3(3216.0f, 3216.0f, 3216.0f));
    mgr.tickPrefetch();
    mgr.endFrame();

    auto newCenter = pack(IRMath::ivec3{100, 100, 100});
    EXPECT_TRUE(mgr.isResident(newCenter));
    EXPECT_FALSE(mgr.isResident(originKey));
}

TEST(ChunkPrefetchTest, ZeroRadiusDisablesPrefetch) {
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 0;
    ChunkResidencyManager mgr{std::move(cfg)};

    mgr.beginFrame(IRMath::vec3(16.0f, 16.0f, 16.0f));
    mgr.tickPrefetch();

    EXPECT_EQ(mgr.residentChunkCount(), 0u);
}

TEST(ChunkPrefetchTest, NegativeFractionalCameraPositionFloorsToCorrectChunk) {
    // Regression: IRMath::ivec3(vec3) truncates toward zero; a fractional
    // negative position like (-0.5,-0.5,-0.5) must floor to (-1,-1,-1),
    // not truncate to (0,0,0). beginFrame's cast uses IRMath::floor() to
    // ensure correct chunk classification.
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    // Camera at world voxel (-0.5, -0.5, -0.5) — floor maps to voxel (-1,-1,-1)
    // which is in chunk (-1,-1,-1) (covers voxels [-32, 0) along each axis).
    mgr.beginFrame(IRMath::vec3(-0.5f, -0.5f, -0.5f));
    mgr.tickPrefetch();

    EXPECT_EQ(mgr.residentChunkCount(), 27u);
    // Ring around chunk (-1,-1,-1): -2..0 on each axis.
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{-1, -1, -1})));
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{-2, -2, -2})));
    EXPECT_TRUE(mgr.isResident(pack(IRMath::ivec3{0, 0, 0})));
    // Chunk (1,1,1) sits outside the radius-1 ring around (-1,-1,-1) —
    // would be incorrectly resident if the truncating cast were still in play.
    EXPECT_FALSE(mgr.isResident(pack(IRMath::ivec3{1, 1, 1})));
}

TEST(ChunkPrefetchTest, PrefetchIdempotentAcrossFrames) {
    ChunkResidencyManager::Config cfg;
    cfg.prefetchRadiusChunks_ = 1;
    ChunkResidencyManager mgr{std::move(cfg)};

    IRMath::vec3 cam(16.0f, 16.0f, 16.0f);
    mgr.beginFrame(cam);
    mgr.tickPrefetch();
    EXPECT_EQ(mgr.residentChunkCount(), 27u);

    // Second tick at same position: count stays the same
    mgr.beginFrame(cam);
    mgr.tickPrefetch();
    EXPECT_EQ(mgr.residentChunkCount(), 27u);
}

} // namespace
