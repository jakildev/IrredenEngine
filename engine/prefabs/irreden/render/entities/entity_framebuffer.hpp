/*
 * Project: Irreden Engine
 * File: entity_framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: December 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef ENTITY_FRAMEBUFFER_H
#define ENTITY_FRAMEBUFFER_H


#include <irreden/ir_ecs.hpp>

#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_framebuffer.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_size_triangles.hpp>

using namespace IRComponents;

namespace IRECS {

    template <>
    struct Prefab<PrefabTypes::kFramebuffer> {
        static EntityId create(
            std::string canvasName,
            ivec2 triangleCanvasSize,
            ivec3 voxelPoolSize,
            ivec2 framebufferSize,
            ivec2 framebufferExtraPixelBufferSize,
            float startZoomLevel = 1.0f
        )
        {
            return IRECS::createEntity(
                C_VoxelPool{voxelPoolSize},
                C_SizeTriangles{triangleCanvasSize},
                C_TriangleCanvasTextures{triangleCanvasSize},
                C_TriangleCanvasFramebuffer{
                    framebufferSize,
                    framebufferExtraPixelBufferSize
                },
                C_Position3D{vec3(0.0f)},
                C_CameraPosition2DIso{vec2(0.0f)},
                C_ZoomLevel{startZoomLevel},
                C_Name{canvasName}
            );
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

#endif /* ENTITY_FRAMEBUFFER_H */
