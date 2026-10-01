#ifndef IR_PREFAB_CANVAS_PART_H
#define IR_PREFAB_CANVAS_PART_H

// Prefab-scoped helpers for composite entities: several voxel sets drawn into
// one host entity's detached re-voxelize canvas, each posed by its own
// transform (`C_CanvasPart`, PROPAGATE_CANVAS_PARTS). The helpers own the
// cross-entity half — moving a part's voxel set between pools, and keeping
// `C_RotationMode` consistent with where the set lives — so a part is never
// resident in one pool while tagged for another's rebuild path.
//
// A hosted part carries `C_RotationMode{DETACHED_REVOXELIZE}` and no
// `C_EntityCanvas` of its own: the mode keeps the GRID rebuild systems off its
// span, the missing canvas keeps the composite from drawing it twice.
//
// Leaving the host for good is `IRPrefab::RotationMode::setMode(part, mode)`,
// which drops the membership and re-homes the set in the same call. Nothing
// here changes a part's transform or its parent.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_part.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <cstddef>
#include <vector>

namespace IRPrefab::CanvasPart {

namespace detail {

/// The re-voxelize canvas `host` currently owns, or `kNullEntity` when it owns
/// no canvas, a forward-scatter one, or a canvas without a pool.
inline IREntity::EntityId hostCanvas(IREntity::EntityId host) {
    if (host == IREntity::kNullEntity || !IREntity::entityExists(host)) {
        return IREntity::kNullEntity;
    }
    auto mode = IREntity::getComponentOptional<IRComponents::C_RotationMode>(host);
    auto canvas = IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(host);
    if (!mode || !canvas ||
        mode.value()->mode_ != IRComponents::RotationMode::DETACHED_REVOXELIZE ||
        !IRPrefab::VoxelPool::hasPool(canvas.value()->canvasEntity_)) {
        return IREntity::kNullEntity;
    }
    return canvas.value()->canvasEntity_;
}

/// Unallocated tail of `canvas`'s pool. Conservative: freed spans inside the
/// live prefix are not counted, so a move this admits always fits.
inline std::size_t poolTailCapacity(IREntity::EntityId canvas) {
    std::size_t capacity = 0;
    IRPrefab::VoxelPool::withPoolByEntity(canvas, [&](IRComponents::C_VoxelPool &pool) {
        capacity = static_cast<std::size_t>(pool.getVoxelPoolSize() - pool.getLiveVoxelCount());
    });
    return capacity;
}

/// Move `part`'s voxel set into `canvas`'s pool and tag the part for the
/// re-voxelize path. False, with the part left exactly as it was, when the
/// part has no set or the pool cannot hold it — a set seeded into a full pool
/// would drop its records.
inline bool moveIntoHostCanvas(IREntity::EntityId part, IREntity::EntityId canvas) {
    auto setOpt = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(part);
    if (!setOpt || setOpt.value()->recordCount() == 0) {
        return false;
    }
    IRComponents::C_VoxelSetNew &set = *setOpt.value();
    if (set.numVoxels_ > 0 && set.canvasEntity_ == canvas) {
        IREntity::setComponent(
            part,
            IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED_REVOXELIZE}
        );
        return true;
    }
    if (set.recordCount() > poolTailCapacity(canvas)) {
        IRE_LOG_ERROR(
            "canvas part {}: host pool on canvas {} cannot hold {} voxels; the part stays GRID",
            part,
            canvas,
            set.recordCount()
        );
        return false;
    }
    IRPrefab::VoxelPool::restageSet(set);
    const bool attached = set.attachToCanvas(canvas);
    IR_ASSERT(attached, "canvas part failed to attach to its host's pool");
    IREntity::setComponent(
        part,
        IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED_REVOXELIZE}
    );
    return true;
}

} // namespace detail

/// True when `entity` is a part currently drawn inside its host's canvas.
inline bool isHosted(IREntity::EntityId entity) {
    auto part = IREntity::getComponentOptional<IRComponents::C_CanvasPart>(entity);
    auto set = IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity);
    if (!part || !set || set.value()->numVoxels_ <= 0) {
        return false;
    }
    const IREntity::EntityId canvas = detail::hostCanvas(part.value()->host_);
    return canvas != IREntity::kNullEntity && set.value()->canvasEntity_ == canvas;
}

/// Make `part` a part of `host`. When the host owns a re-voxelize canvas the
/// part's set moves into it now; otherwise the part keeps rendering where it
/// is and joins the canvas when the host next enters DETACHED_REVOXELIZE.
/// The part's set must be CENTER-anchored: it turns about its entity origin.
/// Returns whether the part is hosted on return.
inline bool attach(IREntity::EntityId part, IREntity::EntityId host) {
    IR_ASSERT(part != host, "an entity cannot be a canvas part of itself");
    IREntity::setComponent(part, IRComponents::C_CanvasPart{host});
    const IREntity::EntityId canvas = detail::hostCanvas(host);
    if (canvas == IREntity::kNullEntity) {
        return false;
    }
    return detail::moveIntoHostCanvas(part, canvas);
}

/// Spawn a box-shaped part of `host`, drawn in the host's canvas when it has
/// one. `local` is the part's own transform; parent it to the host with
/// `IREntity::setParent` to have it ride the host's motion.
inline IREntity::EntityId create(
    IREntity::EntityId host,
    const IRComponents::C_LocalTransform &local,
    IRMath::ivec3 size,
    IRMath::Color color
) {
    const IREntity::EntityId canvas = detail::hostCanvas(host);
    const bool hosted = canvas != IREntity::kNullEntity;
    return IREntity::createEntity(
        local,
        IRComponents::C_VoxelSetNew{size, color, IRComponents::EntityAnchor::CENTER, canvas},
        IRComponents::C_RotationMode{
            hosted ? IRComponents::RotationMode::DETACHED_REVOXELIZE
                   : IRComponents::RotationMode::GRID
        },
        IRComponents::C_CanvasPart{host}
    );
}

/// Move every part of `host` that is not yet hosted into `canvas`. Called when
/// the host enters DETACHED_REVOXELIZE.
inline void adoptParts(IREntity::EntityId host, IREntity::EntityId canvas) {
    std::vector<IREntity::EntityId> parts;
    IREntity::forEachComponent<IRComponents::C_CanvasPart>([&](IREntity::EntityId &id,
                                                               IRComponents::C_CanvasPart &part) {
        if (part.host_ == host) {
            parts.push_back(id);
        }
    });
    for (const IREntity::EntityId part : parts) {
        detail::moveIntoHostCanvas(part, canvas);
    }
}

/// Re-home every voxel set resident on `canvas` to the active canvas, before
/// that canvas is destroyed or stops re-voxelizing. Sets on entities other
/// than `owner` are its parts: they fall back to GRID and keep their
/// membership, so `adoptParts` restores them. With @p partsOnly the owner's
/// own set stays where it is.
///
/// Done here rather than left to the canvas-teardown hook because that hook
/// runs at the destroy drain and leaves the sets staged: they would miss this
/// frame's transform chain, and a creation without SEED_STAGED_VOXELS would
/// never draw them again.
inline void releaseSets(IREntity::EntityId owner, IREntity::EntityId canvas, bool partsOnly) {
    if (!IRPrefab::VoxelPool::hasPool(canvas)) {
        return;
    }
    std::vector<IREntity::EntityId> parts;
    IREntity::forEachComponent<IRComponents::C_VoxelSetNew>([&](IREntity::EntityId &id,
                                                                IRComponents::C_VoxelSetNew &set) {
        if (set.canvasEntity_ != canvas || (partsOnly && id == owner)) {
            return;
        }
        IRPrefab::VoxelPool::restageSet(set);
        set.attachToCanvas();
        if (id != owner) {
            parts.push_back(id);
        }
    });
    for (const IREntity::EntityId part : parts) {
        IREntity::setComponent(
            part,
            IRComponents::C_RotationMode{IRComponents::RotationMode::GRID}
        );
    }
}

/// Take `part` out of its host for good: free its span in the host's pool —
/// the pool drops the cell group in the same call — and drop the membership.
/// The set is left staged when it was hosted; the caller re-homes it. No-op
/// for an entity that is not a part.
inline void leaveHost(IREntity::EntityId part) {
    if (!IREntity::getComponentOptional<IRComponents::C_CanvasPart>(part)) {
        return;
    }
    if (isHosted(part)) {
        IRPrefab::VoxelPool::restageSet(
            *IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(part).value()
        );
    }
    IREntity::removeComponent<IRComponents::C_CanvasPart>(part);
}

} // namespace IRPrefab::CanvasPart

#endif /* IR_PREFAB_CANVAS_PART_H */
