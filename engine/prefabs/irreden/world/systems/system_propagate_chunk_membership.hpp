#ifndef SYSTEM_PROPAGATE_CHUNK_MEMBERSHIP_H
#define SYSTEM_PROPAGATE_CHUNK_MEMBERSHIP_H

// PROPAGATE_CHUNK_MEMBERSHIP (T-359, Epic E E5) — UPDATE pipeline.
//
// Detects when an entity's world position has crossed a chunk boundary,
// updates its C_ChunkMembership, and migrates ownership through the
// creation-supplied ChunkResidencyManager. Identity (EntityId) is
// preserved across migration — Lua references, parent/child relations,
// and stored EntityIds continue to resolve.
//
// Pipeline placement: register in UPDATE after PROPAGATE_TRANSFORM
// (which writes C_WorldTransform from C_LocalTransform plus the parent
// chain) and before any chunk-aware downstream consumer. Per-entity
// detection runs in `tick`; the manager mutation runs in `endTick` so
// migrations are applied as deferred structural changes — mid-tick
// mutation of the residency map would invalidate iteration of the
// migration vector itself, and the residency map's pointer-validity
// contract (chunk_residency.hpp `slot()`) requires migrations be
// batched, not interleaved with per-entity reads.
//
// Rotated-entity interaction (Epic C C6 / #957): rotation acts on
// entity-local space; chunk membership is decided by world-space root
// position. A rotated entity whose AABB straddles two chunks remains a
// single-chunk citizen for residency purposes — `worldToChunk` is fed
// the root translation only, so the rotation does not perturb the
// migration decision. Acceptance criterion (3).
//
// Wiring contract: a creation that opts into world streaming
// constructs both an `IRWorld::ChunkResidencyManager` and the
// `PROPAGATE_CHUNK_MEMBERSHIP` system, then injects the manager
// pointer with `IRPrefab::Chunk::setMembershipMigrationManager` (or
// directly via `getSystemParams<System<PROPAGATE_CHUNK_MEMBERSHIP>>`).
// When the manager pointer is null — single-chunk creations or
// unit tests — the system is a no-op and the per-entity tick still
// updates `C_ChunkMembership` locally so consumers reading it stay
// consistent.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_chunk_membership.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/world/chunk_coord.hpp>
#include <irreden/world/chunk_residency.hpp>

#include <cstddef>
#include <vector>

namespace IRSystem {

template <> struct System<PROPAGATE_CHUNK_MEMBERSHIP> {
    /// Per-entity record appended in `tick`, drained in `endTick`. Keeping
    /// the residency mutation deferred satisfies the
    /// `flushStructuralChanges` contract: the per-entity tick observes a
    /// stable archetype graph, and the `endTick` batch is the only point
    /// at which the residency map's unordered_map is mutated.
    struct MigrationRecord {
        IREntity::EntityId entityId_;
        IRPrefab::Chunk::ChunkKey oldKey_;
        IRPrefab::Chunk::ChunkKey newKey_;
    };

    /// Creation-supplied; null in unit tests + single-chunk creations.
    /// The system's tick still rewrites `C_ChunkMembership` when the
    /// manager is null, so consumers reading membership stay consistent
    /// even without a residency map.
    IRWorld::ChunkResidencyManager *manager_ = nullptr;

    /// Grows to capacity on first use and stays at the high-water mark
    /// thereafter; cleared (not deallocated) in `beginTick` per the
    /// "Allocations in hot tick paths" rule. Steady-state per-frame
    /// migration count is small (Topic 5 of
    /// docs/design/world-streaming.md), so the initial 64-record reserve
    /// covers typical scenes without a first-frame realloc; mass-
    /// projectile scenes grow the vector organically beyond that.
    std::vector<MigrationRecord> migrations_{};
    static constexpr std::size_t kInitialMigrationsCapacity = 64;

    void beginTick() {
        if (migrations_.capacity() == 0) {
            migrations_.reserve(kInitialMigrationsCapacity);
        }
        migrations_.clear();
    }

    void tick(
        IREntity::EntityId id,
        const IRComponents::C_WorldTransform &world,
        IRComponents::C_ChunkMembership &membership
    ) {
        // World-translation is a float vec3 in voxel units. Floor toward
        // -infinity so a position at x = -0.4 classifies into world-voxel
        // -1, not 0 — matches `worldToChunk`'s integer-side contract.
        // Component-wise IRMath::floor first, then cast to ivec3, so the
        // sign at the cast boundary already rounds toward -infinity.
        const IRMath::ivec3 worldVoxel = IRMath::ivec3(IRMath::floor(world.translation_));
        const IRMath::ivec3 newCoord = IRPrefab::Chunk::worldToChunk(worldVoxel);
        if (newCoord == membership.chunkCoord_) {
            return;
        }

        migrations_.push_back(MigrationRecord{
            id,
            IRPrefab::Chunk::pack(membership.chunkCoord_),
            IRPrefab::Chunk::pack(newCoord)
        });
        membership.chunkCoord_ = newCoord;
    }

    void endTick() {
        if (!manager_) {
            return;
        }
        for (const auto &r : migrations_) {
            if (!IREntity::entityExists(r.entityId_)) {
                continue;
            }
            manager_->migrateEntity(r.entityId_, r.oldKey_, r.newKey_);
        }
    }

    static SystemId create() {
        return registerSystem<
            PROPAGATE_CHUNK_MEMBERSHIP,
            IRComponents::C_WorldTransform,
            IRComponents::C_ChunkMembership>("PropagateChunkMembership");
    }
};

} // namespace IRSystem

namespace IRPrefab::Chunk {

/// Inject the creation's `ChunkResidencyManager` into the migration
/// system after both have been constructed. No-op when the system or
/// manager has not been created yet. The system holds a raw pointer;
/// the manager must outlive the system.
inline void setMembershipMigrationManager(
    IRSystem::SystemId systemId, IRWorld::ChunkResidencyManager *manager
) {
    auto *params = IRSystem::getSystemParams<
        IRSystem::System<IRSystem::PROPAGATE_CHUNK_MEMBERSHIP>>(systemId);
    if (!params) {
        IR_LOG_WARN(
            "setMembershipMigrationManager: PROPAGATE_CHUNK_MEMBERSHIP "
            "system not registered (call System<PROPAGATE_CHUNK_MEMBERSHIP>"
            "::create() first); manager pointer not wired."
        );
        return;
    }
    params->manager_ = manager;
}

} // namespace IRPrefab::Chunk

#endif /* SYSTEM_PROPAGATE_CHUNK_MEMBERSHIP_H */
