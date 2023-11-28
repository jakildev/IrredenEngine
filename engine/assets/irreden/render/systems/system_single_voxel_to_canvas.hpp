/*
 * Project: Irreden Engine
 * File: system_rendering_single_voxel_to_canvas.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_SINGLE_VOXEL_TO_CANVAS_H
#define SYSTEM_SINGLE_VOXEL_TO_CANVAS_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRECS {

    template<>
    struct System<RENDERING_SINGLE_VOXEL_TO_CANVAS_FIRST> {
        static SystemId create() {
            static FrameDataVoxelToCanvas frameData{};
            IRRender::createNamedResource<ShaderProgram>(
                "SingleVoxelProgram1",
                std::vector{
                    ShaderStage{
                        IRRender::kFileCompSingleVoxelToIsoTriangleScreen,
                        GL_COMPUTE_SHADER
                    }.getHandle()
                }
            );
            IRRender::createNamedResource<Buffer>(
                "SingleVoxelFrameData",
                nullptr,
                sizeof(FrameDataVoxelToCanvas),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataVoxelToCanvas
            );
            IRRender::createNamedResource<Buffer>(
                "VoxelPositionBuffer",
                nullptr,
                IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
                GL_DYNAMIC_STORAGE_BIT,
                GL_SHADER_STORAGE_BUFFER,
                kBufferIndex_SingleVoxelPositions
            );
            IRRender::createNamedResource<Buffer>(
                "VoxelColorBuffer",
                nullptr,
                IRConstants::kMaxSingleVoxels * sizeof(C_Voxel),
                GL_DYNAMIC_STORAGE_BIT,
                GL_SHADER_STORAGE_BUFFER,
                kBufferIndex_SingleVoxelColors
            );
            return createSystem<
                C_VoxelPool,
                C_TriangleCanvasTextures,
                C_CameraPosition2DIso
            >(
                "SingleVoxelToCanvasFirst",
                []
                (
                    const C_VoxelPool& voxelPool,
                    C_TriangleCanvasTextures& triangleCanvasTextures,
                    const C_CameraPosition2DIso& cameraPosition
                )
                {
                    frameData.canvasOffset_ = cameraPosition.pos_;
                    IRRender::getNamedResource<Buffer>("SingleVoxelFrameData")->subData(
                        0,
                        sizeof(FrameDataVoxelToCanvas),
                        &frameData
                    );
                    triangleCanvasTextures.clear();
                    // triangleCanvasTextures[i].clearWithColor(&tempColor);
                    // TODO: each voxel allocation should have own
                    // voxel GPU buffers as well.
                    IRRender::getNamedResource<Buffer>("VoxelPositionBuffer")->subData(
                        0,
                        voxelPool.getVoxelPoolSize() *
                            sizeof(C_PositionGlobal3D),
                        voxelPool.getPositionGlobals().data()
                    );
                    IRRender::getNamedResource<Buffer>("VoxelColorBuffer")->subData(
                        0,
                        voxelPool.getVoxelPoolSize() *
                            sizeof(C_Voxel),
                        voxelPool.getColors().data()
                    );

                    triangleCanvasTextures.getTextureDistances()->bindImage(
                        1,
                        GL_READ_WRITE,
                        GL_R32I
                    );
                    glDispatchCompute(
                        voxelPool.getVoxelPoolSize(),
                        1,
                        1
                    );
                    glMemoryBarrier(GL_ALL_BARRIER_BITS);
                },
                []() {
                    IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1")->use();
                    IRECS::getComponent<C_CameraPosition2DIso>(
                        IRRender::getCanvas("main")
                    ).pos_ = IRMath::offsetScreenToIsoTriangles(
                        IRRender::getCameraPositionScreen(),
                        IRRender::getTriangleStepSizeScreen()
                    );
                    IRECS::getComponent<C_CameraPosition2DIso>(
                        IRRender::getCanvas("background")
                    ).pos_ =
                        IRMath::offsetScreenToIsoTriangles(
                            IRRender::getCameraPositionScreen(),
                            IRRender::getTriangleStepSizeScreen()
                        );

                    // Write background here for now
                    IRECS::getComponent<C_TriangleCanvasBackground>(
                        IRRender::getCanvas("background")
                    ).clearCanvasWithBackground(
                        IRECS::getComponent<C_TriangleCanvasTextures>(
                            IRRender::getCanvas("background")
                        )
                    );
                }
            );
        }
    };

    template<>
    struct System<RENDERING_SINGLE_VOXEL_TO_CANVAS_SECOND> {
        static SystemId create() {
            IRRender::createNamedResource<ShaderProgram>(
                "SingleVoxel2",
                std::vector{
                    ShaderStage{
                        IRRender::kFileCompSingleVoxelToCanvasSecondPass,
                        GL_COMPUTE_SHADER
                    }.getHandle()
                }
            );
            return createSystem<
                C_VoxelPool,
                C_TriangleCanvasTextures,
                C_CameraPosition2DIso
            >(
                "SingleVoxelToCanvasSecond",
                []
                (
                    const C_VoxelPool& voxelPool,
                    C_TriangleCanvasTextures& triangleCanvasTextures,
                    const C_CameraPosition2DIso& cameraPosition
                )
                {
                    IRRender::getNamedResource<Buffer>("VoxelPositionBuffer")->subData(
                        0,
                        voxelPool.getVoxelPoolSize() *
                            sizeof(C_PositionGlobal3D),
                        voxelPool.getPositionGlobals().data()
                    );
                    IRRender::getNamedResource<Buffer>("VoxelColorBuffer")->subData(
                        0,
                        voxelPool.getVoxelPoolSize() *
                            sizeof(C_Voxel),
                        voxelPool.getColors().data()
                    );
                    triangleCanvasTextures.getTextureColors()->bindImage(
                        0,
                        GL_WRITE_ONLY,
                        GL_RGBA8
                    );
                    triangleCanvasTextures.getTextureDistances()->bindImage(
                        1,
                        GL_READ_ONLY,
                        GL_R32I
                    );
                    glDispatchCompute(
                        voxelPool.getVoxelPoolSize(),
                        1,
                        1
                    );
                    glMemoryBarrier(GL_ALL_BARRIER_BITS);
                },
                []() {
                    IRRender::getNamedResource<ShaderProgram>("SingleVoxel2")->use();
                }
            );
        }
    };

} // namespace System

#endif /* SYSTEM_SINGLE_VOXEL_TO_CANVAS_H */
