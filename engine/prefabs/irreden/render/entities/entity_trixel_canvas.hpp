#ifndef ENTITY_TRIXEL_CANVAS_H
#define ENTITY_TRIXEL_CANVAS_H

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_size_triangles.hpp>

using namespace IRComponents;

namespace IREntity {

template <> struct Prefab<PrefabTypes::kTrixelCanvas> {
    static EntityId
    create(std::string canvasName, ivec2 triangleCanvasSize, EntityId framebuffer = kNullEntity) {
        EntityId canvas = IREntity::createEntity(
            C_SizeTriangles{triangleCanvasSize},
            C_TriangleCanvasTextures{triangleCanvasSize},
            C_Name{canvasName}
        );
        if (framebuffer == kNullEntity) {
            setParent(canvas, getEntity("mainFramebuffer"));
        } else {
            IREntity::setParent(canvas, framebuffer);
        }
        IRE_LOG_INFO(
            "Created trixel canvas {} with framebuffer parent {}, size {},{}",
            canvas,
            framebuffer,
            triangleCanvasSize.x,
            triangleCanvasSize.y
        );
        return canvas;
    }

    static void setColor(EntityId canvas, Color color) {
        IREntity::getComponent<C_TriangleCanvasTextures>(canvas).clearWithColor(color);
    }
};
} // namespace IREntity

#endif /* ENTITY_TRIXEL_CANVAS_H */
