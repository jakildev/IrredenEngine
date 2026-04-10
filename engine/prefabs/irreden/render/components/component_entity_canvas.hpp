#ifndef COMPONENT_ENTITY_CANVAS_H
#define COMPONENT_ENTITY_CANVAS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_EntityCanvas {
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;
    ivec2 canvasSize_;
    bool visible_ = true;

    C_EntityCanvas() : canvasSize_{0} {}

    C_EntityCanvas(ivec2 canvasSize)
        : canvasSize_{canvasSize} {
        canvasEntity_ = IREntity::createEntity(
            C_SizeTriangles{canvasSize},
            C_TriangleCanvasTextures{canvasSize}
        );
        IREntity::EntityId mainFb = IREntity::getEntity("mainFramebuffer");
        if (mainFb != IREntity::kNullEntity) {
            IREntity::setParent(canvasEntity_, mainFb);
        }
    }

    void addVoxelPool(ivec3 poolSize) {
        if (canvasEntity_ != IREntity::kNullEntity) {
            IREntity::setComponent(canvasEntity_, C_VoxelPool{poolSize});
        }
    }

    void resize(ivec2 newSize) {
        canvasSize_ = newSize;
        if (canvasEntity_ != IREntity::kNullEntity) {
            IREntity::setComponent(canvasEntity_, C_SizeTriangles{newSize});
            IREntity::setComponent(canvasEntity_, C_TriangleCanvasTextures{newSize});
        }
    }

    C_TriangleCanvasTextures &getTextures() {
        return IREntity::getComponent<C_TriangleCanvasTextures>(canvasEntity_);
    }

    const C_TriangleCanvasTextures &getTextures() const {
        return IREntity::getComponent<C_TriangleCanvasTextures>(canvasEntity_);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
