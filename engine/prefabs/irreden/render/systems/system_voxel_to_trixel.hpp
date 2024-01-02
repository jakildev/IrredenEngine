/*
 * Project: Irreden Engine
 * File: system_rendering_single_voxel_to_canvas.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_VOXEL_TO_TRIXEL_H
#define SYSTEM_VOXEL_TO_TRIXEL_H

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
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRECS {

    template<>
    struct System<VOXEL_TO_TRIXEL_STAGE_1> {
        static SystemId create() {
            static FrameDataVoxelToCanvas frameData{};
            IRRender::createNamedResource<ShaderProgram>(
                "SingleVoxelProgram1",
                std::vector{
                    ShaderStage{
                        IRRender::kFileCompVoxelToTrixelStage1,
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
                C_TriangleCanvasTextures
            >(
                "SingleVoxelToCanvasFirst",
                []
                (
                    const C_VoxelPool& voxelPool,
                    C_TriangleCanvasTextures& triangleCanvasTextures
                )
                {
                    frameData.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
                    frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(
                        triangleCanvasTextures.size_
                    );

                    IRRender::getNamedResource<Buffer>("SingleVoxelFrameData")->subData(
                        0,
                        sizeof(FrameDataVoxelToCanvas),
                        &frameData
                    );
                    triangleCanvasTextures.clear();
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
                    // IRE_LOG_INFO("Voxel pool size: {}", voxelPool.getVoxelPoolSize());
                    glDispatchCompute(
                        voxelPool.getVoxelPoolSize(),
                        1,
                        1
                    );
                    glMemoryBarrier(GL_ALL_BARRIER_BITS);

                },
                []() {
                    IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1")->use();
                    // IRECS::getComponent<C_CameraPosition2DIso>(
                    //     IRRender::getCanvas("background")
                    // ).pos_ =
                    //     IRMath::offsetScreenToIsoTriangles(
                    //         IRRender::getCameraPositionScreen(),
                    //         IRRender::getTriangleStepSizeScreen()
                    //     );

                    // // Write background here for now
                    // IRECS::getComponent<C_TriangleCanvasBackground>(
                    //     IRRender::getCanvas("background")
                    // ).clearCanvasWithBackground(
                    //     IRECS::getComponent<C_TriangleCanvasTextures>(
                    //         IRRender::getCanvas("background")
                    //     )
                    // );
                },
                []() {

                }
            );
        }
    };

    template<>
    struct System<VOXEL_TO_TRIXEL_STAGE_2> {
        static SystemId create() {
            IRRender::createNamedResource<ShaderProgram>(
                "SingleVoxel2",
                std::vector{
                    ShaderStage{
                        IRRender::kFileCompVoxelToTrixelStage2,
                        GL_COMPUTE_SHADER
                    }.getHandle()
                }
            );
            return createSystem<
                C_VoxelPool,
                C_TriangleCanvasTextures
            >(
                "SingleVoxelToCanvasSecond",
                []
                (
                    const C_VoxelPool& voxelPool,
                    C_TriangleCanvasTextures& triangleCanvasTextures
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
                    // triangleCanvasTextures.textureTriangleColors_.second->saveAsPNG(
                    //     "../save_files/triangleCanvasColors.png"
                    // );

                },
                []() {
                    IRRender::getNamedResource<ShaderProgram>("SingleVoxel2")->use();
                }
            );
        }
    };

} // namespace System

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
