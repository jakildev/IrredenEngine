#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/detached_revoxelize.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_subject_adopt.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string_view>
#include <unordered_set>
#include <vector>

// Every producer of a pool record or active-mask word queues the range it
// wrote, so VOXEL_TO_TRIXEL_STAGE_1 can upload only queued ranges. Each test
// snapshots the pool, runs one producer, and requires every changed element to
// fall inside a queued range — the predicate the stage's assert-build upload
// audit applies. The GPU side of the flush is covered by RecordUploadTest in
// gpu_compute_dispatch_test.cpp.

namespace {

using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

constexpr Color kSetColor{200, 120, 60, 255};

struct PoolSnapshot {
    std::vector<C_Voxel> records_;
    std::vector<std::uint32_t> mask_;
};

PoolSnapshot snapshotAndDrain(C_VoxelPool &pool) {
    pool.clearPendingRecordUploads();
    return {pool.getColors(), pool.getActiveMask()};
}

template <typename T>
std::size_t changedCount(const std::vector<T> &before, const std::vector<T> &after) {
    std::size_t changed = 0;
    for (std::size_t i = 0; i < before.size(); ++i) {
        changed += std::memcmp(&before[i], &after[i], sizeof(T)) != 0 ? 1 : 0;
    }
    return changed;
}

template <typename T>
void expectCovered(
    const std::vector<T> &before,
    const std::vector<T> &after,
    IRSystem::detail::PendingRanges queued,
    std::size_t live,
    std::string_view what
) {
    IRSystem::detail::coalesceRanges(queued, live, 0);
    const std::optional<std::size_t> unqueued = IRSystem::detail::firstUnqueuedChange<T>(
        std::span<const T>(after.data(), live),
        std::span<const T>(before.data(), live),
        queued
    );
    EXPECT_FALSE(unqueued.has_value())
        << what << " element " << unqueued.value_or(0) << " changed without a queued range";
}

// The producer changed something, and every change is queued.
void expectQueuedCoversChanges(C_VoxelPool &pool, const PoolSnapshot &before, bool expectRecords) {
    const std::size_t live = static_cast<std::size_t>(pool.getLiveVoxelCount());
    const std::size_t words =
        (live + IRComponents::kVoxelActiveMaskBits - 1) / IRComponents::kVoxelActiveMaskBits;
    if (expectRecords) {
        EXPECT_GT(changedCount(before.records_, pool.getColors()), 0u) << "producer wrote nothing";
        EXPECT_FALSE(pool.getPendingRecordRanges().empty());
    }
    expectCovered(before.records_, pool.getColors(), pool.getPendingRecordRanges(), live, "record");
    expectCovered(
        before.mask_,
        pool.getActiveMask(),
        pool.getPendingMaskWordRanges(),
        words,
        "mask"
    );
}

class VoxelRecordUploadProducerTest : public ::testing::Test {
  protected:
    IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 4)});
    }

    // A neighbour span on each side, so a producer that queued the wrong span
    // or a span-relative index reads as an unqueued change.
    C_VoxelSetNew &makeSet(IREntity::EntityId canvas, ivec3 size = ivec3(3, 3, 3)) {
        IREntity::createEntity(
            C_VoxelSetNew{ivec3(2, 1, 1), kSetColor, EntityAnchor::CENTER, canvas}
        );
        const IREntity::EntityId entity =
            IREntity::createEntity(C_VoxelSetNew{size, kSetColor, EntityAnchor::CENTER, canvas});
        IREntity::createEntity(
            C_VoxelSetNew{ivec3(2, 1, 1), kSetColor, EntityAnchor::CENTER, canvas}
        );
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    static C_VoxelPool &poolOf(IREntity::EntityId canvas) {
        return IREntity::getComponent<C_VoxelPool>(canvas);
    }

    IREntity::EntityManager m_entityManager;
};

TEST_F(VoxelRecordUploadProducerTest, SetMutatorsQueueWhatTheyWrite) {
    const IREntity::EntityId canvas = makeCanvas();
    C_VoxelSetNew &set = makeSet(canvas);
    C_VoxelPool &pool = poolOf(canvas);

    const auto check = [&](std::string_view name, auto &&mutate) {
        SCOPED_TRACE(name);
        const PoolSnapshot before = snapshotAndDrain(pool);
        mutate();
        expectQueuedCoversChanges(pool, before, true);
    };
    check("changeVoxelColor", [&] { set.changeVoxelColor(ivec3(1, 1, 1), Color{1, 2, 3, 255}); });
    check("changeVoxelColor to inactive", [&] {
        set.changeVoxelColor(ivec3(2, 1, 1), Color{0, 0, 0, 0});
    });
    check("changeVoxelColorAll", [&] { set.changeVoxelColorAll(Color{9, 8, 7, 255}); });
    check("changeVoxelPriority", [&] { set.changeVoxelPriority(ivec3(0, 2, 1), 2); });
    check("changeVoxelPriorityAll", [&] { set.changeVoxelPriorityAll(1); });
    check("deactivateAll", [&] { set.deactivateAll(); });
    check("activateAll", [&] { set.activateAll(); });
    check("fillPlane", [&] {
        set.deactivateAll();
        set.fillPlane(2, 1, Color{40, 50, 60, 255});
    });
    check("reshape", [&] { set.reshape(IRMath::Shape3D::SPHERE); });
    check("editVoxels", [&] {
        set.editVoxels([](int index, C_Voxel &voxel, vec3) {
            voxel.color_ = Color{static_cast<std::uint8_t>(index), 1, 1, 255};
        });
    });
    check("carve", [&] { set.carve([](vec3 local) { return local.x > 0.0f; }); });
    check("resyncAfterRawEdits", [&] {
        set.voxels_[4].bone_id_ = 3;
        set.voxels_[5].deactivate();
        set.resyncAfterRawEdits();
    });
    check("growAllocation", [&] {
        ASSERT_TRUE(set.growAllocation(pool, set.allocatedVoxels_ + 4));
    });
}

TEST_F(VoxelRecordUploadProducerTest, HiddenSetEditsStillQueueTheirRecords) {
    const IREntity::EntityId canvas = makeCanvas();
    C_VoxelSetNew &set = makeSet(canvas);
    C_VoxelPool &pool = poolOf(canvas);
    set.visible_ = false;
    IRPrefab::VoxelPool::markRangeInactive(set.voxelStartIdx_, set.numVoxels_, canvas);

    const PoolSnapshot before = snapshotAndDrain(pool);
    set.changeVoxelColorAll(Color{3, 3, 3, 255});
    expectQueuedCoversChanges(pool, before, true);
}

TEST_F(VoxelRecordUploadProducerTest, PoolMaskMutatorsAndAllocationQueueWhatTheyWrite) {
    C_VoxelPool pool(ivec3(200, 1, 1));
    pool.allocateVoxels(150);
    pool.clearPendingRecordUploads();

    const auto check = [&](std::string_view name, bool records, auto &&mutate) {
        SCOPED_TRACE(name);
        const PoolSnapshot before = snapshotAndDrain(pool);
        mutate();
        expectQueuedCoversChanges(pool, before, records);
        EXPECT_FALSE(pool.getPendingMaskWordRanges().empty());
    };
    check("setActiveBit", false, [&] { pool.setActiveBit(70); });
    check("clearActiveBit", false, [&] { pool.clearActiveBit(70); });
    check("setActiveMaskRange", false, [&] { pool.setActiveMaskRange(5, 120); });
    check("clearActiveMaskRange", false, [&] { pool.clearActiveMaskRange(33, 40); });
    check("resyncActiveMaskFromColors", false, [&] {
        pool.getColors()[90].color_ = Color{1, 1, 1, 255};
        pool.markRecordsChanged(90, 1);
        pool.resyncActiveMaskFromColors(0, 150);
    });
    check("deallocateVoxels", true, [&] { pool.deallocateVoxels(80, 20); });
    check("allocateVoxels reuse", false, [&] { pool.allocateVoxels(20); });
    check("allocateVoxels tail", false, [&] { pool.allocateVoxels(40); });
    check("setFogCarrierPolicy", true, [&] {
        pool.setFogCarrierPolicy(C_VoxelPool::FogCarrierPolicy::BODY, 77);
        pool.setActiveBit(0);
    });
}

#ifndef IR_RELEASE
TEST_F(VoxelRecordUploadProducerTest, QueueingPastThePoolFails) {
    C_VoxelPool pool(ivec3(200, 1, 1));
    EXPECT_THROW(pool.queueRecordRange(190, 20), std::runtime_error);
    EXPECT_NO_THROW(pool.queueRecordRange(190, 10));
}
#endif

TEST_F(VoxelRecordUploadProducerTest, FogCarrierStampQueuesTheSetSpan) {
    const IREntity::EntityId canvas = makeCanvas();
    C_VoxelSetNew &set = makeSet(canvas);
    C_VoxelPool &pool = poolOf(canvas);

    const PoolSnapshot before = snapshotAndDrain(pool);
    IRPrefab::Fog::stampBodyCarrier(pool, set, true, 99);
    expectQueuedCoversChanges(pool, before, true);

    const PoolSnapshot staged = snapshotAndDrain(pool);
    const IRPrefab::Fog::StampedRecords stamped =
        IRPrefab::Fog::stampBodyCarrierStaged(pool, set, true, 140);
    EXPECT_TRUE(pool.getPendingRecordRanges().empty()) << "the staged form leaves the queue alone";
    EXPECT_EQ(stamped.start_, set.voxelStartIdx_);
    EXPECT_EQ(stamped.count_, static_cast<std::size_t>(set.allocatedVoxels_));
    IRPrefab::Fog::queueStamped(stamped);
    expectQueuedCoversChanges(pool, staged, true);
}

TEST_F(VoxelRecordUploadProducerTest, FogAdoptionMergesWorkerStampsIntoThePoolQueue) {
    IRSystem::SystemManager systemManager;
    const IREntity::EntityId canvas = IREntity::createEntity(
        C_VoxelPool{ivec3(16, 16, 4)},
        IRComponents::C_CanvasFogOfWar{IRComponents::C_CanvasFogOfWar::HeadlessInit{}}
    );
    IRRender::setHeadlessActiveCanvasEntity(canvas);
    for (int i = 0; i < 6; ++i) {
        IREntity::createEntity(
            C_VoxelSetNew{ivec3(2, 2, 1), kSetColor, EntityAnchor::CENTER, canvas},
            C_WorldTransform{}
        );
    }
    systemManager.registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT>()}
    );
    C_VoxelPool &pool = poolOf(canvas);

    const PoolSnapshot before = snapshotAndDrain(pool);
    systemManager.executePipeline(IRTime::Events::UPDATE);
    IREntity::flushStructuralChanges();
    expectQueuedCoversChanges(poolOf(canvas), before, true);
    IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
}

TEST_F(VoxelRecordUploadProducerTest, SourceFaceOccupancyRecomputeQueuesTheLivePrefix) {
    const IREntity::EntityId canvas = makeCanvas();
    makeSet(canvas);
    C_VoxelPool &pool = poolOf(canvas);
    std::vector<ivec3> cells;
    std::unordered_set<std::int64_t> occupancy;

    const PoolSnapshot before = snapshotAndDrain(pool);
    IRPrefab::DetachedRevoxelize::detail::recomputeSourceFaceOccupancy(
        pool,
        IRMath::vec4(0.0f, 0.0f, IRMath::sin(0.3f), IRMath::cos(0.3f)),
        cells,
        occupancy
    );
    expectQueuedCoversChanges(pool, before, false);
    EXPECT_FALSE(pool.getPendingRecordRanges().empty());
}

TEST_F(VoxelRecordUploadProducerTest, GridRebuildArmsQueueWhatTheyWrite) {
    const IREntity::EntityId canvas = makeCanvas();
    C_VoxelSetNew &set = makeSet(canvas, ivec3(3, 3, 2));
    C_VoxelPool &pool = poolOf(canvas);
    IRSystem::System<IRSystem::REBUILD_GRID_VOXELS> rebuild;
    C_WorldTransform rotated;
    rotated.rotation_ = IRMath::vec4(0.0f, 0.0f, IRMath::sin(0.4f), IRMath::cos(0.4f));
    C_WorldTransform identity;

    {
        SCOPED_TRACE("inverse");
        const PoolSnapshot before = snapshotAndDrain(pool);
        rebuild.inverseArm(set, rotated, pool);
        expectQueuedCoversChanges(pool, before, true);
    }
    {
        SCOPED_TRACE("forward");
        C_WorldTransform flattened = rotated;
        flattened.scale_ = vec3(1.0f, 1.0f, 0.0f);
        const PoolSnapshot before = snapshotAndDrain(pool);
        rebuild.forwardArm(set, flattened, pool);
        expectQueuedCoversChanges(pool, before, true);
    }
    {
        SCOPED_TRACE("identity restore");
        const PoolSnapshot before = snapshotAndDrain(pool);
        rebuild.identityArm(set, identity, pool);
        expectQueuedCoversChanges(pool, before, true);
    }
    {
        SCOPED_TRACE("steady identity re-derives face bits");
        pool.getColors()[set.voxelStartIdx_].flags_ ^= IRComponents::VoxelFlags::kFaceOccludedMask;
        const PoolSnapshot before = snapshotAndDrain(pool);
        rebuild.identityArm(set, identity, pool);
        expectQueuedCoversChanges(pool, before, true);
    }
}

} // namespace
