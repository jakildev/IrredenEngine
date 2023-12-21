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

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_trixel_canvas_origin.hpp>

#include <irreden/render/entities/entity_framebuffer.hpp>

using namespace IRComponents;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kCanvas> {
        static EntityId create(
            std::string canvasName,
            ivec3 voxelPoolSize,
            ivec2 framebufferSize,
            ivec2 framebufferExtraPixelBufferSize,
            float startZoomLevel = 1.0f
        )
        {
            EntityId framebuffer = IRECS::createEntity<kFramebuffer>(
                canvasName + "Framebuffer",
                framebufferSize,
                framebufferExtraPixelBufferSize,
                startZoomLevel
            );
            ivec2 triangleCanvasSize = IRMath::gameResolutionToSize2DIso(
                framebufferSize + framebufferExtraPixelBufferSize,
                vec2(startZoomLevel)
            );
            EntityId canvas = IRECS::createEntity(
                C_VoxelPool{voxelPoolSize},
                C_SizeTriangles{triangleCanvasSize},
                C_TriangleCanvasTextures{triangleCanvasSize},
                C_Name{canvasName}
            );
            IRECS::setParent(canvas, framebuffer);
            IRE_LOG_INFO("Created canvas {} with framebuffer parent {}, size {},{}",
                canvas,
                framebuffer,
                framebufferSize.x,
                framebufferSize.y
            );
            return canvas;
        }

        static void saveCanvas(
            EntityId canvas,
            std::string filename
        )
        {
            C_TriangleCanvasTextures& canvasTextures =
                IRECS::getComponent<C_TriangleCanvasTextures>(canvas);
            // IRScript::saveCanvas(
            //     canvasTextures,
            //     filename
            // );
        }

    };
}

#endif /* ENTITY_CANVAS_H */
