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
// backed canvas, prefer Prefab<PrefabTypes::kVoxelPoolCanvas>::create
// directly — `addVoxelPool` here is a post-hoc archetype migration that
// triggers a node move.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/entities/entity_trixel_canvas.hpp>
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
    return IRComponents::C_EntityCanvas{canvas, canvasSize};
}

/// Attach a `C_VoxelPool` to the child canvas entity, upgrading it for
/// voxel-pool consumers. No-op if the canvas isn't initialized.
inline void addVoxelPool(
    const IRComponents::C_EntityCanvas &entityCanvas, IRMath::ivec3 poolSize
) {
    if (entityCanvas.canvasEntity_ == IREntity::kNullEntity) return;
    IREntity::setComponent(entityCanvas.canvasEntity_, IRComponents::C_VoxelPool{poolSize});
}

/// Resize the child canvas's textures and update the wrapper's cached
/// size. No-op if the canvas isn't initialized.
inline void resize(IRComponents::C_EntityCanvas &entityCanvas, IRMath::ivec2 newSize) {
    entityCanvas.canvasSize_ = newSize;
    if (entityCanvas.canvasEntity_ == IREntity::kNullEntity) return;
    IREntity::setComponent(entityCanvas.canvasEntity_, IRComponents::C_SizeTriangles{newSize});
    IREntity::setComponent(
        entityCanvas.canvasEntity_, IRComponents::C_TriangleCanvasTextures{newSize}
    );
}

} // namespace IRPrefab::EntityCanvas

#endif /* IR_PREFAB_ENTITY_CANVAS_H */
