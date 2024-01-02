/*
 * Project: Irreden Engine
 * File: entity_canvas.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_CANVAS_H
#define ENTITY_CANVAS_H

#include <irreden/ir_ecs.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_size_triangles.hpp>

using namespace IRComponents;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kTrixelCanvas> {
        static EntityId create(
            std::string canvasName,
            ivec2 triangleCanvasSize,
            EntityId framebuffer = kNullEntity
        )
        {
            EntityId canvas = IRECS::createEntity(
                C_SizeTriangles{triangleCanvasSize},
                C_TriangleCanvasTextures{triangleCanvasSize},
                C_Name{canvasName}
            );
            if(framebuffer == kNullEntity) {
                setParent(canvas, getEntity("mainFramebuffer"));
            }
            else {
                IRECS::setParent(canvas, framebuffer);
            }
            IRE_LOG_INFO("Created trixel canvas {} with framebuffer parent {}, size {},{}",
                canvas,
                framebuffer,
                triangleCanvasSize.x,
                triangleCanvasSize.y
            );
            return canvas;
        }

        static void setColor(EntityId canvas, Color color) {
            IRECS::getComponent<C_TriangleCanvasTextures>(canvas).clearWithColor(color);
        }

    };
}

#endif /* ENTITY_CANVAS_H */
