#ifndef IR_PREFAB_STATIC_AUDIT_H
#define IR_PREFAB_STATIC_AUDIT_H

// The raw-write audit for settled static residents
// (docs/design/static-residents.md, "Forbidden writes").
//
// A raw write to a settled entity's inputs is stored but not derived until
// the entity is resettled or thawed. In a build with assertions, each call
// re-derives a slice of the settled rows from their current inputs and
// asserts that the stored derived state still matches: the world transform,
// the pool global positions of an unrotated set, and the LOD gate. Every
// settled row is covered at least once per `kAuditPeriod` calls. The audit
// only asserts. It never skips or triggers work.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/static_residents.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/render/lod_tier_snapshot.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/grid_rotation.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace IRPrefab::Static {

/// Calls of `audit` within which every settled row is checked once.
inline constexpr std::uint64_t kAuditPeriod = 256;

namespace detail {

#ifndef IR_RELEASE
inline void auditRows(
    IREntity::ArchetypeNode *node,
    int rowBegin,
    int rowEnd,
    const IRPrefab::Lod::TierSnapshot *lod,
    bool auditPositions,
    std::vector<IRComponents::C_WorldTransform> &scratch
) {
    IREntity::EntityManager &entities = IREntity::getEntityManager();
    IRComponents::C_WorldTransform parentWorld{};
    const IREntity::EntityId parent = entities.getParentEntityFromArchetype(node->type_);
    if (parent != IREntity::kNullEntity) {
        if (auto *world =
                IREntity::getComponentOptional<IRComponents::C_WorldTransform>(parent).value_or(
                    nullptr
                )) {
            parentWorld = *world;
        }
    }
    scratch.resize(static_cast<std::size_t>(node->length_));
    IRSystem::System<IRSystem::PROPAGATE_TRANSFORM>::composeNodeRows(
        node,
        parentWorld,
        rowBegin,
        rowEnd,
        IRPrefab::TransformModifier::translationField(),
        IRPrefab::TransformModifier::scaleField(),
        IREntity::getComponentType<IRComponents::C_ResolvedFields>(),
        IREntity::getComponentType<IRComponents::C_Modifiers>(),
        scratch
    );
    const auto &worlds = IREntity::getComponentData<IRComponents::C_WorldTransform>(node);
    const std::vector<IRComponents::C_VoxelSetNew> *sets =
        columnOrNull<IRComponents::C_VoxelSetNew>(node);
    for (int row = rowBegin; row < rowEnd; ++row) {
        const IREntity::EntityId entity = node->entities_[row] & IREntity::IR_ENTITY_ID_BITS;
        IR_ASSERT(
            std::memcmp(&worlds[row], &scratch[row], sizeof(IRComponents::C_WorldTransform)) == 0,
            "Settled static entity {}: its world transform no longer follows from its inputs. "
            "A raw write to a pose input needs IRPrefab::Static::resettle(entity), or thaw "
            "before sustained motion.",
            entity
        );
        if (sets == nullptr) {
            continue;
        }
        const IRComponents::C_VoxelSetNew &set = (*sets)[row];
        if (lod != nullptr) {
            IR_ASSERT(
                set.lodCulled_ ==
                    IRRender::shouldSkipAtLod(set.lodMin_, set.lodMax_, lod->resolve(entity)),
                "Settled static entity {}: its LOD gate no longer follows from its band. "
                "Change the band with C_VoxelSetNew::setLodBand, or resettle the entity.",
                entity
            );
        }
        if (!auditPositions || set.numVoxels_ <= 0 || set.ghostHeld_ ||
            !IRPrefab::GridRotation::isIdentityTransform(worlds[row])) {
            continue;
        }
        const IRComponents::C_VoxelPool *pool =
            IRPrefab::VoxelPool::detail::poolForCanvas(
                set.canvasEntity_ != IREntity::kNullEntity
                    ? set.canvasEntity_
                    : IRPrefab::VoxelPool::activeCanvasEntityOrNull()
            );
        if (pool == nullptr) {
            continue;
        }
        const auto &positions = pool->getPositions();
        const auto &offsets = pool->getPositionOffsets();
        const auto &globals = pool->getPositionGlobals();
        for (int voxel = 0; voxel < set.numVoxels_; ++voxel) {
            const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(voxel);
            if (slot >= globals.size()) {
                break;
            }
            IR_ASSERT(
                globals[slot].pos_ ==
                    positions[slot].pos_ + offsets[slot] + worlds[row].translation_,
                "Settled static entity {}: voxel {} is not at the entity's pose. A raw write "
                "to a pose input or a voxel offset needs IRPrefab::Static::resettle(entity).",
                entity,
                voxel
            );
        }
    }
}
#endif

} // namespace detail

/// One audit step; the World runs it once per fixed update. A no-op in a
/// build without assertions.
inline void audit() {
#ifndef IR_RELEASE
    const IREntity::ComponentId settledTag = detail::settledTagOrNull();
    if (settledTag == IREntity::kNullComponent) {
        return;
    }
    const std::vector<IREntity::ArchetypeNode *> nodes =
        IREntity::queryArchetypeNodesSimple(IREntity::Archetype{settledTag});
    std::uint64_t population = 0;
    for (const IREntity::ArchetypeNode *node : nodes) {
        population += static_cast<std::uint64_t>(node->length_);
    }
    if (population == 0) {
        return;
    }
    // Each check applies only where the creation runs the system that
    // derives the state: without it nothing was derived to compare against.
    const auto runs = [](IRSystem::SystemName name) {
        const IRSystem::SystemId system = IRSystem::findSystem(name);
        return system != IRSystem::kNullSystemId &&
               (IRSystem::isInPipeline(IRTime::Events::UPDATE, system) ||
                IRSystem::isInPipeline(IRTime::Events::RENDER, system));
    };
    if (!runs(IRSystem::PROPAGATE_TRANSFORM)) {
        return;
    }
    const bool auditPositions = runs(IRSystem::UPDATE_VOXEL_SET_CHILDREN);
    const bool auditLod = runs(IRSystem::GATE_VOXEL_SETS_BY_LOD);
    IRPrefab::Lod::TierSnapshot lod;
    if (auditLod) {
        lod.capture();
    }

    IRComponents::C_StaticState &auditState = detail::state();
    std::uint64_t cursor = auditState.auditCursor_ % population;
    std::uint64_t remaining = (population + kAuditPeriod - 1) / kAuditPeriod;
    auditState.auditCursor_ = (cursor + remaining) % population;
    std::vector<IRComponents::C_WorldTransform> scratch;
    // Two passes over the node list cover a slice that wraps past its end.
    for (int pass = 0; pass < 2 && remaining > 0; ++pass) {
        for (IREntity::ArchetypeNode *node : nodes) {
            const std::uint64_t length = static_cast<std::uint64_t>(node->length_);
            if (cursor >= length) {
                cursor -= length;
                continue;
            }
            const std::uint64_t count = remaining < length - cursor ? remaining : length - cursor;
            detail::auditRows(
                node,
                static_cast<int>(cursor),
                static_cast<int>(cursor + count),
                auditLod ? &lod : nullptr,
                auditPositions,
                scratch
            );
            remaining -= count;
            cursor = 0;
            if (remaining == 0) {
                break;
            }
        }
    }
#endif
}

} // namespace IRPrefab::Static

#endif /* IR_PREFAB_STATIC_AUDIT_H */
