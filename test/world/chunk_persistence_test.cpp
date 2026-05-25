#include <gtest/gtest.h>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/world/chunk_coord.hpp>
#include <irreden/world/chunk_persistence.hpp>
#include <irreden/world/chunk_residency.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <list>
#include <span>
#include <string>
#include <vector>

namespace {

using IRComponents::C_Voxel;
using IRMath::Color;
using IRPrefab::Chunk::ChunkKey;
using IRPrefab::Chunk::pack;
using IRWorld::ChunkDiskPersistence;
using IRWorld::ChunkResidencyManager;
using IRWorld::ChunkResidencyState;
using IRWorld::RequestPriority;

constexpr std::size_t kChunkVolume = static_cast<std::size_t>(IRConstants::kChunkSize.x) *
                                     static_cast<std::size_t>(IRConstants::kChunkSize.y) *
                                     static_cast<std::size_t>(IRConstants::kChunkSize.z);

class ChunkPersistenceFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        // Per-test temp directory — temp_directory_path() + a unique id
        // composed from steady_clock + a static counter so back-to-back
        // tests don't share a directory (would race if a previous test
        // crashed without cleanup).
        static std::atomic<std::uint64_t> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_root = std::filesystem::temp_directory_path() /
                 ("ir-chunk-persistence-" + std::to_string(stamp) + "-" +
                  std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(m_root);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::string rootStr() const {
        return m_root.string();
    }

    std::filesystem::path m_root;
};

std::vector<IRAsset::VoxelRecord> makeRecordsWithSentinel(std::uint32_t sentinel) {
    // Build a chunk-volume-sized record vector with a deterministic
    // sentinel pattern so a round-trip mismatch surfaces immediately.
    std::vector<IRAsset::VoxelRecord> records(kChunkVolume);
    for (std::size_t i = 0; i < records.size(); ++i) {
        auto &r = records[i];
        const std::uint8_t alpha = (i % 7u == 0) ? 0u : 255u; // sparse hollow pattern
        r.color_ = Color{
            static_cast<std::uint8_t>((sentinel + i) & 0xFFu),
            static_cast<std::uint8_t>(((sentinel + i) >> 4) & 0xFFu),
            static_cast<std::uint8_t>(((sentinel + i) >> 8) & 0xFFu),
            alpha,
        };
        r.material_id_ = static_cast<std::uint8_t>(i & 0xFFu);
        r.flags_ = static_cast<std::uint8_t>((i >> 1) & 0xFFu);
        r.bone_id_ = static_cast<std::uint8_t>((i >> 2) & 0xFFu);
        r.layer_id_ = static_cast<std::uint8_t>((i >> 3) & 0xFFu);
    }
    return records;
}

TEST_F(ChunkPersistenceFixture, ChunkPathEmbedsSignedAxisFragments) {
    ChunkDiskPersistence persistence{rootStr()};
    auto path = persistence.chunkPath(pack(IRMath::ivec3{0, 0, 0}));
    EXPECT_NE(path.find("/chunks/"), std::string::npos);
    EXPECT_NE(path.find("+00000_+00000_+00000.vxs"), std::string::npos);

    auto negPath = persistence.chunkPath(pack(IRMath::ivec3{-1, 2, -32767}));
    EXPECT_NE(negPath.find("-00001_+00002_-32767.vxs"), std::string::npos);
}

TEST_F(ChunkPersistenceFixture, RoundTripPreservesNonEmptyRecords) {
    // The DENSE-mode .vxs VRLE encoding drops per-field data for slots
    // with alpha == 0 (the empty-run skips them and the loader decodes
    // them as default-constructed VoxelRecord{}); only non-empty slots
    // round-trip full RGB/material/flags/bone/layer. That trade-off is
    // documented in engine/asset/include/irreden/asset/voxel_set_format.hpp
    // ("Trailing empty slots ... are implicit ... Slots not covered by
    // any triple are decoded as VoxelRecord{}"). This test enforces
    // both halves of the contract.
    ChunkDiskPersistence persistence{rootStr()};
    auto key = pack(IRMath::ivec3{3, -7, 11});
    auto records = makeRecordsWithSentinel(0xABCDu);

    auto status = persistence.saveChunk(key, records);
    ASSERT_TRUE(status.ok()) << status.message_;
    EXPECT_TRUE(persistence.chunkExists(key));

    auto loaded = persistence.loadChunk(key);
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->size(), records.size());

    std::size_t nonEmptyCount = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const auto &expect = records[i];
        const auto &got = (*loaded)[i];
        if (expect.color_.alpha_ == 0) {
            EXPECT_EQ(got.color_.alpha_, 0u) << "i=" << i;
            continue;
        }
        ++nonEmptyCount;
        ASSERT_EQ(got.color_.red_, expect.color_.red_) << "i=" << i;
        ASSERT_EQ(got.color_.green_, expect.color_.green_) << "i=" << i;
        ASSERT_EQ(got.color_.blue_, expect.color_.blue_) << "i=" << i;
        ASSERT_EQ(got.color_.alpha_, expect.color_.alpha_) << "i=" << i;
        ASSERT_EQ(got.material_id_, expect.material_id_) << "i=" << i;
        ASSERT_EQ(got.flags_, expect.flags_) << "i=" << i;
        ASSERT_EQ(got.bone_id_, expect.bone_id_) << "i=" << i;
        ASSERT_EQ(got.layer_id_, expect.layer_id_) << "i=" << i;
    }
    EXPECT_GT(nonEmptyCount, 0u) << "test sentinel should leave some non-empty slots";
}

TEST_F(ChunkPersistenceFixture, LoadOnMissingFileReturnsNullopt) {
    ChunkDiskPersistence persistence{rootStr()};
    auto key = pack(IRMath::ivec3{42, 42, 42});
    EXPECT_FALSE(persistence.chunkExists(key));
    EXPECT_FALSE(persistence.loadChunk(key).has_value());
}

TEST_F(ChunkPersistenceFixture, SaveWithMismatchedSizeReportsErrorAndDoesNotWrite) {
    ChunkDiskPersistence persistence{rootStr()};
    auto key = pack(IRMath::ivec3{0, 0, 0});
    std::vector<IRAsset::VoxelRecord> short_records(kChunkVolume - 1);
    auto status = persistence.saveChunk(key, short_records);
    EXPECT_FALSE(status.ok());
    EXPECT_FALSE(persistence.chunkExists(key));
}

TEST_F(ChunkPersistenceFixture, ChunkFilesLandUnderChunksSubdirectory) {
    ChunkDiskPersistence persistence{rootStr()};
    auto key = pack(IRMath::ivec3{1, 2, 3});
    auto status = persistence.saveChunk(key, makeRecordsWithSentinel(0x1u));
    ASSERT_TRUE(status.ok()) << status.message_;
    EXPECT_TRUE(std::filesystem::exists(m_root / "chunks"));
    EXPECT_TRUE(std::filesystem::is_directory(m_root / "chunks"));
}

// ── ChunkResidencyManager integration with persistence ──────────────────

// FakePool owns the backing voxel vectors for each chunk allocation so
// the spans returned to the manager stay valid for the test's lifetime.
// std::list never invalidates element addresses on insertion — important
// because the manager holds spans into the vectors that follow.
class FakePool {
  public:
    IRRender::VoxelPoolAllocation allocate(unsigned int size) {
        m_allocations.emplace_back(size);
        auto &vec = m_allocations.back();
        IRRender::VoxelPoolAllocation alloc{};
        alloc.startIndex_ = m_nextOffset;
        alloc.voxels_ = std::span<C_Voxel>{vec.data(), vec.size()};
        m_nextOffset += size;
        return alloc;
    }

  private:
    std::list<std::vector<C_Voxel>> m_allocations;
    std::size_t m_nextOffset = 0;
};

TEST_F(ChunkPersistenceFixture, ManagerSavesDirtyChunkOnEvictAndLoadsOnReResident) {
    ChunkDiskPersistence persistence{rootStr()};
    FakePool pool;

    ChunkResidencyManager::Config cfg;
    cfg.persistence_ = &persistence;
    cfg.voxelsPerChunk_ = static_cast<unsigned int>(kChunkVolume);
    cfg.poolAllocator_ = [&pool](unsigned int size) { return pool.allocate(size); };

    ChunkResidencyManager mgr{std::move(cfg)};
    auto key = pack(IRMath::ivec3{4, -2, 8});

    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);
    auto *s = mgr.slot(key);
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->state_, ChunkResidencyState::RESIDENT);
    ASSERT_EQ(s->poolAllocation_.voxels_.size(), kChunkVolume);
    EXPECT_FALSE(s->isDirty()) << "fresh-allocated chunk should not be dirty";

    // Mutate a few voxels through the pool slice and route the dirty
    // flag through the manager's API (the load-bearing contract).
    s->poolAllocation_.voxels_[0] = C_Voxel{
        Color{10, 20, 30, 255},
        /*mat=*/1,
        /*flags=*/2,
        /*bone=*/3,
        /*layer=*/4
    };
    s->poolAllocation_.voxels_[42] = C_Voxel{Color{99, 88, 77, 66}};
    s->poolAllocation_.voxels_[kChunkVolume - 1] =
        C_Voxel{Color{0, 0, 0, 0}}; // explicit empty slot to test alpha-0 round-trip
    mgr.markChunkDirty(key);
    EXPECT_TRUE(s->isDirty()) << "markChunkDirty should flip the slot's bit";

    mgr.requestEvict(key);
    EXPECT_FALSE(mgr.isResident(key));
    EXPECT_TRUE(persistence.chunkExists(key));

    // Re-request — fresh allocation from FakePool, but the load path
    // should seed it from the file written above.
    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);
    auto *s2 = mgr.slot(key);
    ASSERT_NE(s2, nullptr);
    ASSERT_EQ(s2->poolAllocation_.voxels_.size(), kChunkVolume);

    EXPECT_EQ(s2->poolAllocation_.voxels_[0].color_.red_, 10);
    EXPECT_EQ(s2->poolAllocation_.voxels_[0].material_id_, 1);
    EXPECT_EQ(s2->poolAllocation_.voxels_[0].flags_, 2);
    EXPECT_EQ(s2->poolAllocation_.voxels_[0].bone_id_, 3);
    EXPECT_EQ(s2->poolAllocation_.voxels_[0].layer_id_, 4);
    EXPECT_EQ(s2->poolAllocation_.voxels_[42].color_.red_, 99);
    EXPECT_EQ(s2->poolAllocation_.voxels_[kChunkVolume - 1].color_.alpha_, 0u);
    EXPECT_FALSE(s2->isDirty()) << "post-load slice should not be dirty";
}

TEST_F(ChunkPersistenceFixture, ManagerSkipsSaveForCleanChunkOnEvict) {
    ChunkDiskPersistence persistence{rootStr()};
    FakePool pool;
    ChunkResidencyManager::Config cfg;
    cfg.persistence_ = &persistence;
    cfg.voxelsPerChunk_ = static_cast<unsigned int>(kChunkVolume);
    cfg.poolAllocator_ = [&pool](unsigned int size) { return pool.allocate(size); };

    ChunkResidencyManager mgr{std::move(cfg)};
    auto key = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);
    // No mutation, dirty stays false.
    mgr.requestEvict(key);
    EXPECT_FALSE(persistence.chunkExists(key)) << "clean chunk should not write a file on evict";
}

TEST_F(ChunkPersistenceFixture, FlushPendingSavesWritesDirtySlotsAndClearsDirty) {
    ChunkDiskPersistence persistence{rootStr()};
    FakePool pool;
    ChunkResidencyManager::Config cfg;
    cfg.persistence_ = &persistence;
    cfg.voxelsPerChunk_ = static_cast<unsigned int>(kChunkVolume);
    cfg.poolAllocator_ = [&pool](unsigned int size) { return pool.allocate(size); };

    ChunkResidencyManager mgr{std::move(cfg)};
    auto k0 = pack(IRMath::ivec3{0, 0, 0});
    auto k1 = pack(IRMath::ivec3{1, 0, 0});
    mgr.requestResident(k0, RequestPriority::VISIBLE_RENDER);
    mgr.requestResident(k1, RequestPriority::VISIBLE_RENDER);

    auto *s0 = mgr.slot(k0);
    ASSERT_NE(s0, nullptr);
    s0->poolAllocation_.voxels_[0] = C_Voxel{Color{1, 2, 3, 255}};
    mgr.markChunkDirty(k0);
    // k1 stays clean.

    mgr.flushPendingSaves();
    EXPECT_TRUE(persistence.chunkExists(k0));
    EXPECT_FALSE(persistence.chunkExists(k1)) << "clean slot should not flush";
    EXPECT_FALSE(mgr.slot(k0)->isDirty()) << "dirty bit clears on successful save";

    // Slots remain resident after flushPendingSaves.
    EXPECT_TRUE(mgr.isResident(k0));
    EXPECT_TRUE(mgr.isResident(k1));
}

TEST_F(ChunkPersistenceFixture, RequestResidentWithNoPriorSaveLeavesSliceFreshlyAllocated) {
    ChunkDiskPersistence persistence{rootStr()};
    FakePool pool;
    ChunkResidencyManager::Config cfg;
    cfg.persistence_ = &persistence;
    cfg.voxelsPerChunk_ = static_cast<unsigned int>(kChunkVolume);
    cfg.poolAllocator_ = [&pool](unsigned int size) { return pool.allocate(size); };

    ChunkResidencyManager mgr{std::move(cfg)};
    auto key = pack(IRMath::ivec3{9, 9, 9});
    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);

    auto *s = mgr.slot(key);
    ASSERT_NE(s, nullptr);
    // FakePool default-constructs C_Voxel which sets color to (0,0,0,255).
    EXPECT_EQ(s->poolAllocation_.voxels_[0].color_.alpha_, 255u);
    EXPECT_FALSE(s->isDirty());
}

TEST_F(ChunkPersistenceFixture, NoPersistenceWiredLeavesEvictBehaviorUnchanged) {
    // Regression guard: a manager constructed without persistence must
    // behave exactly like the T-297 baseline — never touch the
    // filesystem under any code path.
    FakePool pool;
    ChunkResidencyManager::Config cfg;
    cfg.voxelsPerChunk_ = static_cast<unsigned int>(kChunkVolume);
    cfg.poolAllocator_ = [&pool](unsigned int size) { return pool.allocate(size); };
    ChunkResidencyManager mgr{std::move(cfg)};

    auto key = pack(IRMath::ivec3{0, 0, 0});
    mgr.requestResident(key, RequestPriority::VISIBLE_RENDER);
    mgr.markChunkDirty(key);
    mgr.requestEvict(key);

    // Nothing should have landed in the temp root because no
    // persistence was wired; explicit check on the directory contents.
    EXPECT_TRUE(std::filesystem::is_empty(m_root));
}

} // namespace
