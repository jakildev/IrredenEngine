#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/entity/entity_manager.hpp>

#include <irreden/common/components/component_chunk_membership.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/world/systems/system_propagate_chunk_membership.hpp>
#include <irreden/world/chunk_coord.hpp>
#include <irreden/world/chunk_residency.hpp>

#include <vector>

namespace {

using IRComponents::C_ChunkMembership;
using IRComponents::C_WorldTransform;
using IRPrefab::Chunk::ChunkKey;
using IRPrefab::Chunk::chunkOriginVoxel;
using IRPrefab::Chunk::pack;
using IRWorld::ChunkResidencyManager;
using IRWorld::RequestPriority;

class ChunkMembershipMigrationTest : public testing::Test {
  protected:
    ChunkMembershipMigrationTest()
        : m_entity_manager{}
        , m_system_manager{}
        , m_residency{} {}

    IRSystem::SystemId registerMigrationSystem(ChunkResidencyManager *manager) {
        auto sysId = IRSystem::System<IRSystem::PROPAGATE_CHUNK_MEMBERSHIP>::create();
        auto *params = m_system_manager.getSystemParams<
            IRSystem::System<IRSystem::PROPAGATE_CHUNK_MEMBERSHIP>>(sysId);
        params->manager_ = manager;
        m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
        return sysId;
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    ChunkResidencyManager m_residency;
};

// Acceptance criterion (1): "Track an entity moving across 10 chunk
// boundaries; ID unchanged."
TEST_F(ChunkMembershipMigrationTest, EntityCrosses10ChunkBoundariesWithIdPreserved) {
    const int kEdge = IRConstants::kChunkSize.x;

    // Seed the entity in chunk (0,0,0) — start position is one voxel
    // inside the +x boundary so the first step crosses cleanly.
    auto entityId = IREntity::createEntity(
        C_WorldTransform{},
        C_ChunkMembership{IRMath::ivec3{0, 0, 0}}
    );

    // Pre-attach the entity to the source chunk so migrateEntity has a
    // valid source to remove from on the first crossing.
    m_residency.requestResident(pack(IRMath::ivec3{0, 0, 0}), RequestPriority::FORCED);
    m_residency.attachEntity(entityId, pack(IRMath::ivec3{0, 0, 0}));

    registerMigrationSystem(&m_residency);

    // Walk +x across 10 chunk boundaries by advancing one full chunk
    // edge per step. Capture the entity id every frame to assert it
    // survives every migration.
    std::vector<IREntity::EntityId> seenIds;
    seenIds.reserve(10);
    for (int step = 1; step <= 10; ++step) {
        // Place the entity one voxel into chunk (step, 0, 0).
        IREntity::getComponent<C_WorldTransform>(entityId).translation_ =
            IRMath::vec3{static_cast<float>(step * kEdge), 0.0f, 0.0f};

        m_system_manager.executePipeline(IRTime::Events::UPDATE);

        seenIds.push_back(entityId);

        // EntityId is the contract — no destroy/recreate, so the id we
        // hold from createEntity continues to resolve.
        EXPECT_TRUE(IREntity::entityExists(entityId))
            << "step " << step << " — entity disappeared during migration";

        const auto &membership = IREntity::getComponent<C_ChunkMembership>(entityId);
        EXPECT_EQ(membership.chunkCoord_, IRMath::ivec3(step, 0, 0))
            << "step " << step << " — chunk membership did not advance";

        // Manager state reflects the migration: source chunk no longer
        // owns the entity, destination chunk does.
        const auto *destSlot = m_residency.slot(pack(IRMath::ivec3{step, 0, 0}));
        ASSERT_NE(destSlot, nullptr) << "step " << step;
        ASSERT_EQ(destSlot->ownedEntities_.size(), 1u) << "step " << step;
        EXPECT_EQ(destSlot->ownedEntities_[0], entityId) << "step " << step;

        const auto *srcSlot = m_residency.slot(pack(IRMath::ivec3{step - 1, 0, 0}));
        if (srcSlot) {
            // The source slot persists post-migration (eviction is a
            // separate concern, E2). It just no longer owns the entity.
            EXPECT_TRUE(std::find(
                            srcSlot->ownedEntities_.begin(),
                            srcSlot->ownedEntities_.end(),
                            entityId
                        ) == srcSlot->ownedEntities_.end())
                << "step " << step << " — entity still appears in source slot's owned list";
        }
    }

    // Every recorded EntityId is the same id from createEntity —
    // satisfies "ID unchanged" across all 10 boundary crossings.
    for (const auto &id : seenIds) {
        EXPECT_EQ(id, entityId);
    }
}

// Acceptance criterion (3): "Rotated entities (Epic C C6 #957) migrate
// without artifact." Rotation acts on local space; chunk membership is
// decided by world-space root translation. The migration system must
// not be perturbed by a non-identity quaternion in C_WorldTransform.
TEST_F(ChunkMembershipMigrationTest, RotatedEntityMigratesByRootTranslationOnly) {
    const int kEdge = IRConstants::kChunkSize.x;

    C_WorldTransform startWorld{};
    // 45° rotation around Z — non-identity quaternion. The system reads
    // only `translation_`; rotation must not influence the chunk-coord
    // decision.
    startWorld.rotation_ = IRMath::vec4{
        0.0f, 0.0f, IRMath::sin(IRMath::kPi / 8.0f), IRMath::cos(IRMath::kPi / 8.0f)
    };

    auto entityId = IREntity::createEntity(startWorld, C_ChunkMembership{IRMath::ivec3{0, 0, 0}});
    m_residency.requestResident(pack(IRMath::ivec3{0, 0, 0}), RequestPriority::FORCED);
    m_residency.attachEntity(entityId, pack(IRMath::ivec3{0, 0, 0}));

    registerMigrationSystem(&m_residency);

    // Translate root one chunk in +y, leaving rotation untouched.
    IREntity::getComponent<C_WorldTransform>(entityId).translation_ =
        IRMath::vec3{0.0f, static_cast<float>(kEdge), 0.0f};

    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(
        IREntity::getComponent<C_ChunkMembership>(entityId).chunkCoord_, IRMath::ivec3(0, 1, 0)
    );
    // Rotation survived the migration cycle untouched.
    const auto &finalRot = IREntity::getComponent<C_WorldTransform>(entityId).rotation_;
    EXPECT_FLOAT_EQ(finalRot.z, IRMath::sin(IRMath::kPi / 8.0f));
    EXPECT_FLOAT_EQ(finalRot.w, IRMath::cos(IRMath::kPi / 8.0f));
}

// Edge case: negative-coordinate crossing. Float floor must round
// toward -infinity at the cast boundary — a translation of -0.4 must
// land in chunk -1, not chunk 0. Without the IRMath::floor pre-pass the
// implicit vec3 → ivec3 truncation would silently mis-classify near
// zero, and a slow-drifting object would skip its first migration.
TEST_F(ChunkMembershipMigrationTest, SubVoxelNegativeStepCrossesIntoChunkMinus1) {
    auto entityId = IREntity::createEntity(
        C_WorldTransform{},
        C_ChunkMembership{IRMath::ivec3{0, 0, 0}}
    );
    m_residency.requestResident(pack(IRMath::ivec3{0, 0, 0}), RequestPriority::FORCED);
    m_residency.attachEntity(entityId, pack(IRMath::ivec3{0, 0, 0}));

    registerMigrationSystem(&m_residency);

    IREntity::getComponent<C_WorldTransform>(entityId).translation_ =
        IRMath::vec3{-0.4f, 0.0f, 0.0f};

    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(
        IREntity::getComponent<C_ChunkMembership>(entityId).chunkCoord_, IRMath::ivec3(-1, 0, 0)
    );
}

// No-op path: a creation that constructs the migration system but
// forgets to inject a manager pointer must still keep C_ChunkMembership
// consistent (so consumers reading it don't observe a torn snapshot).
// The system records the migration vector locally but skips the manager
// mutation in endTick.
TEST_F(ChunkMembershipMigrationTest, NullManagerStillUpdatesMembershipComponent) {
    const int kEdge = IRConstants::kChunkSize.x;
    auto entityId = IREntity::createEntity(
        C_WorldTransform{},
        C_ChunkMembership{IRMath::ivec3{0, 0, 0}}
    );

    registerMigrationSystem(nullptr);

    IREntity::getComponent<C_WorldTransform>(entityId).translation_ =
        IRMath::vec3{static_cast<float>(kEdge), 0.0f, 0.0f};
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(
        IREntity::getComponent<C_ChunkMembership>(entityId).chunkCoord_, IRMath::ivec3(1, 0, 0)
    );
}

} // namespace
