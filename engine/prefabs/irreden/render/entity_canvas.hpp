#ifndef IR_PREFAB_ENTITY_CANVAS_H
#define IR_PREFAB_ENTITY_CANVAS_H

// Prefab-scoped helpers for C_EntityCanvas. The component itself is plain
// data (an EntityId pointing at a child canvas entity, plus size/visible
// fields); cross-entity orchestration — spawning the child, parenting it
// to mainFramebuffer, swapping its textures on resize — lives here so
// the component layout stays trivial and archetype-iteration friendly.
//
// `create` delegates to Prefab<PrefabTypes::kTrixelCanvas> so the canvas-
// entity bundle (textures + size + name) and parent-to-mainFramebuffer
// behavior stays defined in one place. If a creation needs a voxel-pool-
// backed canvas (a detached entity hosting voxel geometry), use
// `createWithVoxelPool` — it spawns the pool up front so no post-hoc
// archetype migration is needed. `addVoxelPool` remains for the rarer
// case of upgrading an already-spawned textures-only canvas.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/entities/entity_trixel_canvas.hpp>
#include <irreden/render/entities/entity_voxel_pool_canvas.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <string>

namespace IRPrefab::EntityCanvas {

/// Spawn a child canvas entity (textures + size + name) parented to
/// `mainFramebuffer`, and return a `C_EntityCanvas` that wraps it. Add
/// the returned component to the parent entity that should host the
/// sub-canvas. The name lets debug tooling and entity-by-name lookup
/// find the child later.
inline IRComponents::C_EntityCanvas create(std::string canvasName, IRMath::ivec2 canvasSize) {
    IREntity::EntityId canvas =
        IREntity::Prefab<IREntity::PrefabTypes::kTrixelCanvas>::create(canvasName, canvasSize);
    IREntity::setComponent(canvas, IRComponents::C_DetachedCanvas{});
    return IRComponents::C_EntityCanvas{canvas, canvasSize};
}

/// Spawn a child canvas entity that owns a `C_VoxelPool` from the start
/// (textures + pool + size + name), parented to `mainFramebuffer`, and
/// return a `C_EntityCanvas` wrapping it. Use this — rather than
/// `create` + `addVoxelPool` — when the detached canvas will host voxel
/// geometry: the pool is present at construction so `C_VoxelSetNew`
/// targeting this canvas allocates cleanly with no post-hoc archetype
/// migration.
///
/// World-placed by default (`screenLocked = false`, the #1624 convention):
/// the canvas also gets `C_TrixelCanvasRenderBehavior` + `C_CanvasAOTexture`
/// attached here, putting it in the `COMPUTE_VOXEL_AO` + `LIGHTING_TO_TRIXEL`
/// archetypes so it participates in world lighting without every call site
/// having to remember the pair (#2322 D1 — a spawn site that forgot them
/// silently composited raw albedo). Pass `screenLocked = true` for a genuine
/// overlay (HUD prop, billboard, floating showcase); it skips both
/// components entirely, matching the fixed-depth overlay contract.
///
/// The `!screenLocked` attach is unconditional: the builder can't see a
/// creation's `--no-lighting` (or equivalent) flag, so a world-placed canvas
/// still allocates a (never-written) `C_CanvasAOTexture` under that flag.
/// Harmless — the texture is small and canvas-sized — but it means this is not
/// a byte-for-byte "skip all lighting-adjacent allocation" path; pass
/// `screenLocked = true` at the call to opt a canvas out of the pair entirely.
inline IRComponents::C_EntityCanvas createWithVoxelPool(
    std::string canvasName,
    IRMath::ivec2 canvasSize,
    IRMath::ivec3 voxelPoolSize,
    bool screenLocked = false
) {
    IREntity::EntityId canvas = IREntity::Prefab<IREntity::PrefabTypes::kVoxelPoolCanvas>::create(
        canvasName,
        voxelPoolSize,
        canvasSize
    );
    IREntity::setComponent(canvas, IRComponents::C_DetachedCanvas{});
    if (!screenLocked) {
        IREntity::setComponent(canvas, IRComponents::C_TrixelCanvasRenderBehavior{});
        IREntity::setComponent(canvas, IRComponents::C_CanvasAOTexture{canvasSize});
    }
    return IRComponents::C_EntityCanvas{canvas, canvasSize, /*visible*/ true, screenLocked};
}

/// Attach a `C_VoxelPool` to the child canvas entity, upgrading it for
/// voxel-pool consumers, and a `C_CanvasLocalRotation` (which
/// `VOXEL_TO_TRIXEL_STAGE_1` requires on every voxel-pool canvas — a
/// `kTrixelCanvas`-spawned canvas lacks it). No-op if the canvas isn't
/// initialized. Prerequisite: the canvas entity must already carry
/// `C_DetachedCanvas` so it is excluded from the `TRIXEL_TO_FRAMEBUFFER`
/// full-screen pass. `EntityCanvas::create` and `createWithVoxelPool` attach
/// the tag automatically; if you constructed the canvas entity by another
/// path, call `IREntity::setComponent(canvas, C_DetachedCanvas{})` first.
inline void addVoxelPool(const IRComponents::C_EntityCanvas &entityCanvas, IRMath::ivec3 poolSize) {
    if (entityCanvas.canvasEntity_ == IREntity::kNullEntity)
        return;
    IREntity::setComponent(entityCanvas.canvasEntity_, IRComponents::C_VoxelPool{poolSize});
    IREntity::setComponent(entityCanvas.canvasEntity_, IRComponents::C_CanvasLocalRotation{});
}

/// Resize the child canvas's textures and update the wrapper's cached
/// size. No-op if the canvas isn't initialized.
inline void resize(IRComponents::C_EntityCanvas &entityCanvas, IRMath::ivec2 newSize) {
    entityCanvas.canvasSize_ = newSize;
    if (entityCanvas.canvasEntity_ == IREntity::kNullEntity)
        return;
    IREntity::setComponent(entityCanvas.canvasEntity_, IRComponents::C_SizeTriangles{newSize});
    IREntity::setComponent(
        entityCanvas.canvasEntity_,
        IRComponents::C_TriangleCanvasTextures{newSize}
    );
}

} // namespace IRPrefab::EntityCanvas

#endif /* IR_PREFAB_ENTITY_CANVAS_H */
