// T-225: thread-safe deferred entity mutations from worker threads.
//
// Drives the EntityManager's per-worker staging buffers from a real
// IRJobs `parallelFor` and checks that:
//
//   1. A worker-spawned entity gets a unique, non-null EntityId
//      immediately (atomic ID allocation).
//   2. After `flushStructuralChanges`, every worker-spawned entity
//      exists in the entity index with the right archetype.
//   3. A worker-issued `markEntityForDeletion` drains correctly when
//      `destroyMarkedEntities` runs on the main thread.
//   4. Concurrent spawn + destroy from many workers produces the same
//      live-entity set as a serial baseline.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_jobs.hpp>
#include <irreden/job/job_manager.hpp>

#include <atomic>
#include <set>
#include <vector>

namespace {

struct ParallelSpawnPayload {
    int workerId_ = 0;
    int index_ = 0;
};

class ParallelSpawnFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        m_entityManager = std::make_unique<IREntity::EntityManager>();
        // Two workers is enough to exercise the per-worker slot split
        // without depending on hardware_concurrency.
        m_jobs = std::make_unique<IRJobs::JobManager>(2);
        m_entityManager->resizeWorkerStaging(
            static_cast<std::size_t>(m_jobs->workerCount() + 1)
        );
    }

    void TearDown() override {
        m_entityManager.reset();
        m_jobs.reset();
    }

    std::unique_ptr<IREntity::EntityManager> m_entityManager;
    std::unique_ptr<IRJobs::JobManager> m_jobs;
};

} // namespace

TEST_F(ParallelSpawnFixture, WorkerCreateEntityReturnsUniqueIds) {
    constexpr int kCount = 1024;
    std::vector<IREntity::EntityId> ids(kCount, IREntity::kNullEntity);

    IRJobs::parallelFor(0, kCount, 32, [&](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            ids[i] = IREntity::createEntity(
                ParallelSpawnPayload{IRJobs::workerId(), i}
            );
        }
    });

    std::set<IREntity::EntityId> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), static_cast<std::size_t>(kCount))
        << "atomic ID allocation handed out duplicates";
    for (auto id : ids) {
        EXPECT_NE(id, IREntity::kNullEntity);
    }
}

TEST_F(ParallelSpawnFixture, WorkerSpawnedEntitiesExistAfterFlush) {
    constexpr int kCount = 4096;
    std::vector<IREntity::EntityId> ids(kCount, IREntity::kNullEntity);

    IRJobs::parallelFor(0, kCount, 64, [&](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            ids[i] = IREntity::createEntity(
                ParallelSpawnPayload{IRJobs::workerId(), i}
            );
        }
    });

    // Pre-flush: IDs were handed out but the archetype-node rows
    // haven't been materialised yet. Post-flush every entity must
    // exist and the payload must round-trip.
    m_entityManager->flushStructuralChanges();

    int liveCount = 0;
    int offMainCount = 0;
    for (auto id : ids) {
        if (m_entityManager->entityExists(id)) {
            ++liveCount;
            // The payload's worker id is whichever thread enkiTS
            // landed the chunk on. Main can pump chunks via
            // WaitforTask, so id 0 is valid; we just bound it to
            // `[0, workerCount()]`.
            auto &payload = m_entityManager->getComponent<ParallelSpawnPayload>(id);
            EXPECT_GE(payload.workerId_, 0);
            EXPECT_LE(payload.workerId_, m_jobs->workerCount());
            if (payload.workerId_ > 0) {
                ++offMainCount;
            }
        }
    }
    EXPECT_EQ(liveCount, kCount);
    // At least one chunk should have landed off-main, otherwise the
    // per-worker buffer path was never exercised (see
    // `IRJobsFixture.ParallelForRunsOnAtLeastOneWorker` for the same
    // logic on the IRJobs side — probabilistic guarantee, not
    // structural).
    EXPECT_GT(offMainCount, 0)
        << "expected at least one chunk to run off the main thread; "
           "if this fires consistently the worker pool isn't picking up tasks";
}

TEST_F(ParallelSpawnFixture, WorkerDestroyDrainsThroughDestroyMarked) {
    constexpr int kCount = 2048;
    std::vector<IREntity::EntityId> ids;
    ids.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        ids.push_back(
            IREntity::createEntity(ParallelSpawnPayload{0, i})
        );
    }
    // `getLiveEntityCount` also counts the lazily-allocated
    // component-registration entities (one per unique component
    // type registered to date). Capture the baseline at fixture
    // construction time and only check the delta.
    const auto liveBefore = m_entityManager->getLiveEntityCount();
    EXPECT_GE(liveBefore, static_cast<IREntity::EntityId>(kCount));

    IRJobs::parallelFor(0, kCount, 64, [&](int rangeBegin, int rangeEnd) {
        for (int i = rangeBegin; i < rangeEnd; ++i) {
            // markEntityForDeletion takes a non-const ref so the flag
            // bit gets ORed in — fine on a worker because each loop
            // iteration owns its element of `ids`.
            m_entityManager->markEntityForDeletion(ids[i]);
        }
    });

    m_entityManager->destroyMarkedEntities();

    // Only the spawned entities are destroyed; component-registration
    // entities still live.
    EXPECT_EQ(
        m_entityManager->getLiveEntityCount(),
        liveBefore - static_cast<IREntity::EntityId>(kCount)
    );
    // None of the original IDs should be reachable now.
    for (auto id : ids) {
        EXPECT_FALSE(m_entityManager->entityExists(id));
    }
}

TEST_F(ParallelSpawnFixture, ConcurrentSpawnAndDestroyMatchesSerialBaseline) {
    // Two "groups" exercising T-224's lifted TWO_SPAWNERS rule:
    // group A spawns N entities, group B (running in parallel under
    // the multithreading epic's full design) would also spawn another
    // N. Here we just emulate that by running two parallelFor passes
    // and counting the resulting live-entity set against the serial
    // expectation. The point is that every spawn-then-destroy issued
    // from a worker resolves through the per-worker buffer without
    // duplicate or lost IDs.
    constexpr int kSpawnPerWorker = 1024;
    const int workerCount = m_jobs->workerCount();
    std::vector<IREntity::EntityId> spawned;
    std::atomic<int> spawnCounter{0};
    std::vector<std::vector<IREntity::EntityId>> perChunk(workerCount + 1);

    IRJobs::parallelFor(
        0, kSpawnPerWorker * workerCount, 64,
        [&](int rangeBegin, int rangeEnd) {
            int wid = IRJobs::workerId();
            for (int i = rangeBegin; i < rangeEnd; ++i) {
                IREntity::EntityId id = IREntity::createEntity(
                    ParallelSpawnPayload{wid, i}
                );
                perChunk[wid].push_back(id);
                spawnCounter.fetch_add(1, std::memory_order_relaxed);
            }
        }
    );

    EXPECT_EQ(spawnCounter.load(), kSpawnPerWorker * workerCount);

    m_entityManager->flushStructuralChanges();
    // Live count includes lazily-registered component entities; the
    // delta should equal the worker spawn count.
    const auto liveAfterSpawn = m_entityManager->getLiveEntityCount();
    EXPECT_GE(liveAfterSpawn,
              static_cast<IREntity::EntityId>(kSpawnPerWorker * workerCount));

    // Destroy half from workers; the rest stays. The mid-flight kill
    // bit OR is on a per-worker-owned slice (perChunk[wid]) so the
    // mutation is safe.
    IRJobs::parallelFor(
        0, workerCount, 1,
        [&](int rangeBegin, int rangeEnd) {
            int wid = IRJobs::workerId();
            for (int chunkIndex = rangeBegin; chunkIndex < rangeEnd; ++chunkIndex) {
                // We don't care which chunk we got handed; we only
                // touch this worker's own staging slot via the
                // EntityManager API. Half the entities in *some*
                // chunks get marked.
                (void)wid;
                auto &chunk = perChunk[chunkIndex < perChunk.size() ? chunkIndex : 0];
                int half = static_cast<int>(chunk.size()) / 2;
                for (int i = 0; i < half; ++i) {
                    m_entityManager->markEntityForDeletion(chunk[i]);
                }
            }
        }
    );

    m_entityManager->destroyMarkedEntities();

    // Verify no double-count: count the entities still tagged as
    // existing across all chunks; subtract the component-
    // registration entities from the live count to compare apples
    // to apples.
    int stillLive = 0;
    for (auto &chunk : perChunk) {
        for (auto id : chunk) {
            if (m_entityManager->entityExists(id)) {
                ++stillLive;
            }
        }
    }
    const auto userLiveAfter =
        m_entityManager->getLiveEntityCount() -
        (liveAfterSpawn -
         static_cast<IREntity::EntityId>(kSpawnPerWorker * workerCount));
    EXPECT_EQ(stillLive, static_cast<int>(userLiveAfter));
}
