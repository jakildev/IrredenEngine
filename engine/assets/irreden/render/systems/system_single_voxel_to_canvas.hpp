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
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>

#include <irreden/voxel/systems/system_voxel_pool.hpp>
#include <irreden/update/systems/system_update_screen_view.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

// TODO: initalize buffer based on GPU stats, and make multiple to
// make up the difference
constexpr int kMaxSingleVoxels =
    IRMath::multVecComponents(IRConstants::kVoxelPoolSize);

namespace IRECS {

    template<>
    class System<RENDERING_SINGLE_VOXEL_TO_CANVAS> : public SystemBase<
        RENDERING_SINGLE_VOXEL_TO_CANVAS,
        C_VoxelPool,
        C_TriangleCanvasTextures,
        C_CameraPosition2DIso
    >   {
    public:
        System()
        :   m_shaderCompute{
                {
                    ShaderStage{
                        IRRender::kFileCompSingleVoxelToIsoTriangleScreen,
                        GL_COMPUTE_SHADER
                    }.getHandle()
                }
            }
        ,   m_shaderComputeSecondPass{
                {
                    ShaderStage{
                        IRRender::kFileCompSingleVoxelToCanvasSecondPass,
                        GL_COMPUTE_SHADER
                    }.getHandle()
                }
            }
        ,   m_bufferVoxelPositions{
                nullptr,
                kMaxSingleVoxels * sizeof(C_Position3D),
                GL_DYNAMIC_STORAGE_BIT,
                GL_SHADER_STORAGE_BUFFER,
                kBufferIndex_SingleVoxelPositions
            }
        ,   m_bufferVoxelColors{
                nullptr,
                kMaxSingleVoxels * sizeof(C_Voxel),
                GL_DYNAMIC_STORAGE_BIT,
                GL_SHADER_STORAGE_BUFFER,
                kBufferIndex_SingleVoxelColors
            }
        ,   m_frameData{}
        ,   m_bufferFrameData{
                nullptr,
                sizeof(FrameDataVoxelToCanvas),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataVoxelToCanvas
            }
        ,   m_player{kNullEntity}
        {


            IRProfile::engLogInfo("Created system RENDERING_SINGLE_VOXEL_TO_CANVAS");
        }
        // instead

        // each allocation and texture components make up a layer kinda
        // Left off around here
        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            const std::vector<C_VoxelPool>& voxelPools, // perhaps scene
            const std::vector<C_TriangleCanvasTextures>& triangleCanvasTextures,
            const std::vector<C_CameraPosition2DIso>& cameraPositions
        )
        {
            // Stage 1
            for(int i = 0; i < entities.size(); i++) {
                updateTriangleCanvasOffset(
                    cameraPositions[i].pos_
                );
                triangleCanvasTextures[i].clear();
                // triangleCanvasTextures[i].clearWithColor(&tempColor);
                // TODO: each voxel allocation should have own
                // voxel GPU buffers as well.
                m_bufferVoxelPositions.subData(
                    0,
                    voxelPools[i].getVoxelPoolSize() *
                        sizeof(C_PositionGlobal3D),
                    voxelPools[i].getPositionGlobals().data()
                );
                m_bufferVoxelColors.subData(
                    0,
                    voxelPools[i].getVoxelPoolSize() *
                        sizeof(C_Voxel),
                    voxelPools[i].getColors().data()
                );

                triangleCanvasTextures[i].getTextureDistances()->bindImage(
                    1,
                    GL_READ_WRITE,
                    GL_R32I
                );
                m_shaderCompute.use();
                glDispatchCompute(
                    voxelPools[i].getVoxelPoolSize(),
                    1,
                    1
                );
                glMemoryBarrier(GL_ALL_BARRIER_BITS);
            }

            // Stage 2
            for(int i = 0; i < entities.size(); i++) {
                // TODO: this subdata wont be needed when each one
                // gets own space in buffer...
                m_bufferVoxelPositions.subData(
                    0,
                    voxelPools[i].getVoxelPoolSize() *
                        sizeof(C_PositionGlobal3D),
                    voxelPools[i].getPositionGlobals().data()
                );
                m_bufferVoxelColors.subData(
                    0,
                    voxelPools[i].getVoxelPoolSize() *
                        sizeof(C_Voxel),
                    voxelPools[i].getColors().data()
                );
                triangleCanvasTextures[i].getTextureColors()->bindImage(
                    0,
                    GL_WRITE_ONLY,
                    GL_RGBA8
                );
                triangleCanvasTextures[i].getTextureDistances()->bindImage(
                    1,
                    GL_READ_ONLY,
                    GL_R32I
                );
                m_shaderComputeSecondPass.use();
                glDispatchCompute(
                    voxelPools[i].getVoxelPoolSize(),
                    1,
                    1
                );
                glMemoryBarrier(GL_ALL_BARRIER_BITS);
            }
        }

        void setPlayer(EntityId player) {

            m_player = player;
        }

        void updateTriangleCanvasOffset(vec2 pos) {
            m_frameData.canvasOffset_ = pos;
            m_bufferFrameData.subData(
                0,
                sizeof(FrameDataVoxelToCanvas),
                &m_frameData
            );
        }

    private:
        EntityId m_player;
        ShaderProgram m_shaderCompute;
        ShaderProgram m_shaderComputeSecondPass;
        Buffer m_bufferVoxelPositions;
        Buffer m_bufferVoxelColors;
        FrameDataVoxelToCanvas m_frameData;
        Buffer m_bufferFrameData;

        virtual void beginExecute() override {
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

        virtual void endExecute() override {
            // TODO move to where m_playerCanvas is defined
            // if(m_player.id_ != kNullEntityId) {
            //     IRECS::getComponent<C_Position3D>(m_playerCanvas).pos_ =
            //         m_player.get<C_Position3D>().pos_;
            // }
            // else {
            // This should be somewhere else for sure
            IRECS::getComponent<C_Position3D>(
                IRRender::getCanvas("player")
            ).pos_ = vec3(0.0f);
            // }
        }
    };


} // namespace System

#endif /* SYSTEM_SINGLE_VOXEL_TO_CANVAS_H */
