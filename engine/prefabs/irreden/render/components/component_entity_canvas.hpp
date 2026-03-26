#ifndef COMPONENT_ENTITY_CANVAS_H
#define COMPONENT_ENTITY_CANVAS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_name.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_EntityCanvas {
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;
    ivec2 canvasSize_;
    bool visible_ = true;

    C_EntityCanvas() : canvasSize_{0} {}

    C_EntityCanvas(ivec2 canvasSize, ivec3 voxelPoolSize, IREntity::EntityId parentCanvas)
        : canvasSize_{canvasSize} {
        canvasEntity_ = IREntity::createEntity(
            C_VoxelPool{voxelPoolSize},
            C_SizeTriangles{canvasSize},
            C_TriangleCanvasTextures{canvasSize}
        );
        if (parentCanvas != IREntity::kNullEntity) {
            IREntity::setParent(canvasEntity_, parentCanvas);
        }
    }

    C_TriangleCanvasTextures &getTextures() {
        return IREntity::getComponent<C_TriangleCanvasTextures>(canvasEntity_);
    }

    C_VoxelPool &getPool() {
        return IREntity::getComponent<C_VoxelPool>(canvasEntity_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
