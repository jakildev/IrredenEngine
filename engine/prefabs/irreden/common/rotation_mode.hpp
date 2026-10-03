#ifndef IR_PREFAB_ROTATION_MODE_H
#define IR_PREFAB_ROTATION_MODE_H

// Prefab-scoped helpers for `C_RotationMode`. The component itself is
// plain data; cross-entity orchestration — allocating the per-entity
// canvas on the detached modes, destroying it on GRID — lives here so
// the component layout stays trivial and archetype-iteration friendly.
//
// Spawn-time mode selection is handled by `IRPrefab::Prefab::spawnPrefab`
// directly. Use `setMode` to change an already-spawned entity's mode at
// runtime; it preserves the rest of the entity's components and pays
// the re-allocation cost (one canvas-entity create or destroy) inline.
//
// Both lifecycle sites read `ownsEntityCanvas` rather than each spelling
// out which modes own a canvas.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

#include <string>
#include <utility>

namespace IRPrefab::RotationMode {

struct SetModeOptions {
    std::string canvasName_{};
    IRMath::ivec2 canvasSize_{};
    // Voxel pool extent of the allocated canvas. Zero derives it from the
    // entity's own voxel set, which only fits that set: a host that carries
    // canvas parts names the extent that spans all of them.
    IRMath::ivec3 poolSize_{};
    bool screenLocked_ = false;
    int depthPriority_ = 0;
};

namespace detail {

inline IRMath::ivec3 detachedPoolSize(IRMath::ivec3 extent) {
    constexpr float kSqrt3 = 1.7320508075688772935f;
    return IRMath::ivec3{
        static_cast<int>(IRMath::ceil(static_cast<float>(extent.x) * kSqrt3)) + 1,
        static_cast<int>(IRMath::ceil(static_cast<float>(extent.y) * kSqrt3)) + 1,
        static_cast<int>(IRMath::ceil(static_cast<float>(extent.z) * kSqrt3)) + 1,
    };
}

inline IRMath::ivec2 detachedCanvasSize(IRMath::ivec3 extent) {
    constexpr int kPixelsPerVoxel = 12;
    const int maxExtent = IRMath::max(IRMath::max(extent.x, extent.y), extent.z);
    const int edge = IRMath::max(maxExtent, 1) * kPixelsPerVoxel;
    return IRMath::ivec2{edge, edge};
}

/// Stamp `entity`'s canvas with its pose now. PROPAGATE_CANVAS_ROTATION does
/// this every frame, but a canvas created or re-tagged after that system ran
/// would otherwise reach RENDER unposed.
inline void stampCanvasPose(IREntity::EntityId entity, IRComponents::RotationMode mode) {
    auto canvas = IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(entity);
    auto world = IREntity::getComponentOptional<IRComponents::C_WorldTransform>(entity);
    if (!canvas || !world) {
        return;
    }
    auto pose = IREntity::getComponentOptional<IRComponents::C_CanvasLocalRotation>(
        canvas.value()->canvasEntity_
    );
    if (!pose) {
        return;
    }
    IRPrefab::CanvasPose::write(
        *pose.value(),
        IRMath::quatInverse(IRPrefab::Camera::getRotationQuat()),
        *world.value(),
        mode,
        *canvas.value()
    );
}

} // namespace detail

/// True when `mode` keeps the entity's rotation on a per-entity
/// `C_EntityCanvas`. DETACHED and DETACHED_REVOXELIZE differ in how that
/// canvas is *filled* — a 2D forward-scatter deform versus a per-frame
/// re-voxelize at full-rotation cell positions — not in whether one
/// exists. Both allocate at spawn and both must release at teardown.
///
/// Canvas lifecycle is decided in exactly two places — `setMode` and
/// `IRPrefab::Prefab::spawnPrefab` — and both call this predicate rather
/// than spelling their own mode list, so a new mode cannot be wired into
/// one site and missed by the other. Adding a mode means classifying it
/// here; `test/ecs/rotation_mode_set_test.cpp` static-asserts the enum's
/// size so that stays mandatory rather than remembered.
inline constexpr bool ownsEntityCanvas(IRComponents::RotationMode mode) {
    return mode == IRComponents::RotationMode::DETACHED ||
           mode == IRComponents::RotationMode::DETACHED_REVOXELIZE;
}

/// Transition an entity to `newMode`, allocating or destroying its
/// per-entity canvas as required.
///
/// - GRID → a canvas-owning mode: allocates a private voxel-pool canvas,
///   re-stages the entity's voxel set into it, and attaches `C_EntityCanvas`.
/// - A canvas-owning mode → GRID: re-homes every voxel set on the entity's
///   canvas to the active canvas, destroys the `C_EntityCanvas` child entity
///   (freeing its GPU textures via `onDestroy`) and removes the component
///   from `entity`.
/// - DETACHED ↔ DETACHED_REVOXELIZE: keeps the existing canvas. Both
///   modes own one, so the swap is a re-tag, not a re-allocation.
/// - Same mode in/out: a no-op **when the canvas already matches the
///   mode**. When it does not, the call reconciles it.
///
/// A canvas part (`C_CanvasPart`) passed here leaves its host first and
/// becomes an independent entity in `newMode`: its span in the host's pool is
/// freed, and its set is resident in its new home, before the call returns.
/// A host's own parts follow the host instead: they move into its canvas
/// whenever it enters DETACHED_REVOXELIZE and fall back to GRID when it
/// leaves that mode.
///
/// Every set this call moves is resident in its destination pool on return,
/// and a canvas it allocates is posed. Call it ahead of the UPDATE transform
/// chain (PROPAGATE_TRANSFORM through the voxel rebuild systems) and the
/// entity is drawn exactly once on every frame across the switch; a call made
/// after that chain draws the moved sets at stale positions for one frame.
///
/// The mismatch case is load-bearing, not defensive. `spawnPrefab`
/// deliberately tags an entity into a canvas-owning mode *without*
/// allocating when it runs with no `RenderManager` and documents this
/// call as the recovery once one exists.
/// Gating the early return on the mode alone would make that recovery a
/// no-op and strand the entity canvas-less.
///
/// `options` is only consulted when a canvas is allocated. Empty dimensions
/// derive a conservative canvas size from the voxel-set extent.
inline void
setMode(IREntity::EntityId entity, IRComponents::RotationMode newMode, SetModeOptions options) {
    using IRComponents::C_EntityCanvas;
    using IRComponents::C_LocalTransform;
    using IRComponents::C_RotationMode;
    using IRComponents::C_VoxelSetNew;

    // Before any component pointer is taken: dropping the membership is a
    // structural change.
    IRPrefab::CanvasPart::leaveHost(entity);

    auto modeOpt = IREntity::getComponentOptional<C_RotationMode>(entity);
    const IRComponents::RotationMode current =
        modeOpt ? modeOpt.value()->mode_ : IRComponents::RotationMode::GRID;

    auto canvasOpt = IREntity::getComponentOptional<C_EntityCanvas>(entity);
    const bool hasCanvas = canvasOpt.has_value();
    const bool wantsCanvas = ownsEntityCanvas(newMode);
    const bool reVoxelize = newMode == IRComponents::RotationMode::DETACHED_REVOXELIZE;

    if (current == newMode && hasCanvas == wantsCanvas) {
        // A part that just left a released host is already where GRID draws it.
        return;
    }

    // Release only when leaving the canvas-owning family entirely — a
    // DETACHED ↔ DETACHED_REVOXELIZE swap keeps the canvas it already has.
    // Read the child id out before destroying anything; the component
    // pointer is not held across the structural change.
    if (hasCanvas && !wantsCanvas) {
        const IREntity::EntityId canvas = canvasOpt.value()->canvasEntity_;
        if (canvas != IREntity::kNullEntity) {
            IRPrefab::CanvasPart::releaseSets(entity, canvas, /*partsOnly=*/false);
            IREntity::destroyEntity(canvas);
        }
        IREntity::removeComponent<C_EntityCanvas>(entity);
    } else if (hasCanvas && !reVoxelize) {
        // Only the re-voxelize fill poses sets independently; under the
        // forward-scatter deform a hosted part would turn with the host.
        IRPrefab::CanvasPart::releaseSets(
            entity,
            canvasOpt.value()->canvasEntity_,
            /*partsOnly=*/true
        );
    } else if (wantsCanvas && !hasCanvas) {
        auto voxelSetOpt = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
        if (!voxelSetOpt || voxelSetOpt.value()->recordCount() == 0) {
            IRE_LOG_ERROR(
                "setMode() requires a non-empty C_VoxelSetNew before entering a detached mode"
            );
            return;
        }
        IR_ASSERT(
            IRRender::g_renderManager != nullptr,
            "setMode() into a canvas-owning rotation mode requires a live RenderManager"
        );
        C_VoxelSetNew &voxelSet = *voxelSetOpt.value();
        const IRMath::ivec3 extent = voxelSet.size_;
        const IRMath::ivec2 canvasSize = options.canvasSize_.x > 0 && options.canvasSize_.y > 0
                                             ? options.canvasSize_
                                             : detail::detachedCanvasSize(extent);
        const IRMath::ivec3 poolSize =
            options.poolSize_.x > 0 && options.poolSize_.y > 0 && options.poolSize_.z > 0
                ? options.poolSize_
                : detail::detachedPoolSize(extent);
        const std::string canvasName = options.canvasName_.empty()
                                           ? "rotation_mode_" + std::to_string(entity)
                                           : options.canvasName_;
        C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
            canvasName,
            canvasSize,
            poolSize,
            options.screenLocked_
        );
        canvas.depthPriority_ = options.depthPriority_;

        IRPrefab::VoxelPool::restageSet(voxelSet);
        const bool attached = voxelSet.attachToCanvas(canvas.canvasEntity_);
        IR_ASSERT(attached, "setMode() failed to attach C_VoxelSetNew to the private canvas");
        IREntity::setComponent(entity, canvas);
    }

    if (!wantsCanvas && (hasCanvas || ownsEntityCanvas(current))) {
        auto localTransform = IREntity::getComponentOptional<C_LocalTransform>(entity);
        if (localTransform) {
            localTransform.value()->unbounded_ = false;
        }
        // A set that reached here staged — a part that left its host — is
        // seeded into the active canvas now rather than by a later seed pass.
        auto voxelSet = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
        if (voxelSet) {
            voxelSet.value()->attachToCanvas();
        }
    }

    IREntity::setComponent(entity, C_RotationMode{newMode});

    if (wantsCanvas) {
        detail::stampCanvasPose(entity, newMode);
        const IREntity::EntityId canvas =
            IREntity::getComponent<C_EntityCanvas>(entity).canvasEntity_;
        if (reVoxelize && IRPrefab::VoxelPool::hasPool(canvas)) {
            IRPrefab::CanvasPart::adoptParts(entity, canvas);
        }
    }
}

inline void setMode(
    IREntity::EntityId entity,
    IRComponents::RotationMode newMode,
    std::string canvasName = {},
    IRMath::ivec2 canvasSize = IRMath::ivec2{0}
) {
    SetModeOptions options;
    options.canvasName_ = std::move(canvasName);
    options.canvasSize_ = canvasSize;
    setMode(entity, newMode, std::move(options));
}

} // namespace IRPrefab::RotationMode

#endif /* IR_PREFAB_ROTATION_MODE_H */
