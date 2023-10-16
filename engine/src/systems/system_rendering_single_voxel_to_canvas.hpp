/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_rendering_single_voxel_to_canvas.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_RENDERING_SINGLE_VOXEL_TO_CANVAS_H
#define SYSTEM_RENDERING_SINGLE_VOXEL_TO_CANVAS_H

#include "..\ecs\ir_system_base.hpp"
#include "..\rendering\ir_rendering.hpp"
#include "..\rendering\buffer.hpp"
#include "..\rendering\shader.hpp"
#include "..\shaders\shader_names.hpp"

#include "..\components\component_tags_all.hpp"
#include "..\components\component_position_global_3D.hpp"
#include "..\components\component_position_2d.hpp"
#include "..\components\component_position_3d.hpp"
#include "..\components\component_camera_position_2d_iso.hpp"
#include "..\components\component_voxel.hpp"
#include "..\components\component_voxel_pool.hpp"
#include "..\components\component_rendering_triangle_canvas_textures.hpp"
#include "..\components\component_rendering_triangle_framebuffer.hpp"
#include "..\components\component_triangle_canvas_background.hpp"
#include "..\components\component_zoom_level.hpp"
#include "..\components\component_texture_scroll.hpp"

#include "system_voxel_pool.hpp"
#include "system_update_screen_view.hpp"

#include "..\math\color_palettes.hpp"

using namespace IRComponents;
using namespace IRMath;
using namespace IRRendering;

// TODO: initalize buffer based on GPU stats, and make multiple to
// make up the difference
constexpr int kMaxSingleVoxels =
    IRMath::multVecComponents(IRConstants::kVoxelPoolSize);

namespace IRECS {

    template<>
    class IRSystem<RENDERING_SINGLE_VOXEL_TO_CANVAS> : public IRSystemBase<
        RENDERING_SINGLE_VOXEL_TO_CANVAS,
        C_VoxelPool,
        C_TriangleCanvasTextures,
        C_CameraPosition2DIso
        // C_TriangleCanvasBackground
        // C_TriangleCanvasFramebuffer
    >   {
    public:
        IRSystem()
        :   m_shaderCompute{
                ShaderStage{
                    IRShaders::kFileCompSingleVoxelToIsoTriangleScreen,
                    GL_COMPUTE_SHADER
                }.getHandle()
            }
        ,   m_shaderComputeSecondPass{
                ShaderStage{
                    IRShaders::kFileCompSingleVoxelToCanvasSecondPass,
                    GL_COMPUTE_SHADER
                }.getHandle()
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
        ,   m_mainCanvas{}
        ,   m_playerCanvas{}
        ,   m_backgroundCanvas{}
        ,   m_player{0}
        {
            int backgroundZoomLevel = 2;
            m_backgroundCanvas.set(C_TriangleCanvasTextures{
                ivec2(
                    IRConstants::kScreenTriangleMaxCanvasSize / uvec2(backgroundZoomLevel)

                )
            });
            m_backgroundCanvas.set(C_TriangleCanvasFramebuffer{
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelNoBuffer
            });
            m_backgroundCanvas.set(C_Position3D{
                vec3(0.0f)
            });
            // TODO: Add back pixel buffer for pixel perfect texture
            // scrolling
            m_backgroundCanvas.set(C_CameraPosition2DIso{vec2(0.0f)});
            std::vector<Color> colorPalette = {
                kPinkTanOrange[1],
                // kPinkTanOrange[2]
                IRColors::kBlack
            };
            m_backgroundCanvas.set(C_TriangleCanvasBackground{
                // BackgroundTypes::kSingleColor,
                // {Color{150, 5, 40, 255}},
                // ivec2(IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer)
                BackgroundTypes::kGradientRandom,
                colorPalette,
                // createColorPaletteFromFile(
                //     "IRMT/data/color_palettes/neon-moon-tarot-5.png"
                // ),
                ivec2(IRConstants::kScreenTriangleMaxCanvasSize) / ivec2(backgroundZoomLevel)

            });
            m_backgroundCanvas.set(C_ZoomLevel{static_cast<float>(backgroundZoomLevel)});
            m_backgroundCanvas.set(C_TextureScrollPosition{vec2(0.0f)});
            m_backgroundCanvas.set(C_TextureScrollVelocity
                {
                    // vec2(1.0f, 1.0f)
                    // vec2(IRConstants::kGameResolution) /
                    // vec2(IRConstants::kScreenTriangleMaxCanvasSize) * vec2(2.0f)
                }
            );
            m_backgroundCanvas.set(C_BackgroundCanvas{});

            // MAIN CANVAS------------------------------------------------------
            m_mainCanvas.set<C_VoxelPool>(
                global.systemManager_->get<VOXEL_POOL>()->getVoxelPoolComponent(0)
            );
            m_mainCanvas.set(C_TriangleCanvasTextures{
                ivec2(IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer)
            });
            // m_mainCanvas.set(C_TriangleCanvasFramebuffer{});
            m_mainCanvas.set(C_TriangleCanvasFramebuffer{
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelBuffer
            });
            m_mainCanvas.set(C_Position3D{vec3(0.0f)});
            m_mainCanvas.set(C_CameraPosition2DIso{vec2(0.0f)}); // gets updated elsewhere
            m_mainCanvas.set(C_ZoomLevel{1.0f});
            m_mainCanvas.set(C_MainCanvas{});

            // m_mainCanvas.set(C_TriangleCanvasBackground{
            //     BackgroundTypes::kSingleColor,
            //     {Color{150, 5, 40, 255}},
            //     ivec2(IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer)
            // });

            // PLAYER CANVAS---------------------------------------------------
            m_playerCanvas.set(C_VoxelPool{
                global.systemManager_->get<VOXEL_POOL>()->getVoxelPoolComponent(1)
            });
            m_playerCanvas.set(C_TriangleCanvasTextures{
                ivec2(IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer)
            });
            m_playerCanvas.set(C_TriangleCanvasFramebuffer{
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelBuffer
            });
            m_playerCanvas.set(C_Position3D{
                vec3(0.0f) // needs to follow player and adjust camera based on player movement
            });
            m_playerCanvas.set(C_CameraPosition2DIso{vec2(0.0f)});
            m_playerCanvas.set(C_ZoomLevel{1.0f});

            // m_playerCanvas.set(C_TriangleCanvasBackground{});

            // GUI CANVAS------------------------------------------------------
            // gui canvas should have triangle cursor for now
            // triangle cursor should be a 2d triangle entity
            // which I also must implement
            m_guiCanvas.set(C_GuiCanvas{});
            // TODO...........

            ENG_LOG_INFO("Created system RENDERING_SINGLE_VOXEL_TO_CANVAS");
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
                    voxelPools[i].positionGlobals_.size() *
                        sizeof(C_PositionGlobal3D),
                    voxelPools[i].positionGlobals_.data()
                );
                m_bufferVoxelColors.subData(
                    0,
                    voxelPools[i].voxels_.size() *
                        sizeof(C_Voxel),
                    voxelPools[i].voxels_.data()
                );

                triangleCanvasTextures[i].getTextureDistances()->bindImage(
                    1,
                    GL_READ_WRITE,
                    GL_R32I
                );
                m_shaderCompute.use();
                glDispatchCompute(
                    voxelPools[i].numVoxels_,
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
                    voxelPools[i].positionGlobals_.size() *
                        sizeof(C_PositionGlobal3D),
                    voxelPools[i].positionGlobals_.data()
                );
                m_bufferVoxelColors.subData(
                    0,
                    voxelPools[i].voxels_.size() *
                        sizeof(C_Voxel),
                    voxelPools[i].voxels_.data()
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
                    voxelPools[i].numVoxels_,
                    1,
                    1
                );
                glMemoryBarrier(GL_ALL_BARRIER_BITS);
            }
        }

        void setPlayer(const EntityId player) {

            m_player = EntityHandle{player};
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
        EntityHandle m_backgroundCanvas;
        EntityHandle m_mainCanvas;
        EntityHandle m_playerCanvas;
        EntityHandle m_guiCanvas;
        EntityHandle m_player;
        ShaderProgram m_shaderCompute;
        ShaderProgram m_shaderComputeSecondPass;
        Buffer m_bufferVoxelPositions;
        Buffer m_bufferVoxelColors;
        FrameDataVoxelToCanvas m_frameData;
        Buffer m_bufferFrameData;

        virtual void beginExecute() override {
             m_mainCanvas.get<C_CameraPosition2DIso>().pos_ =
                offsetScreenToIsoTriangles(
                    global.systemManager_->get<SCREEN_VIEW>()->
                        getGlobalCameraOffsetScreen(),
                    global.systemManager_->get<SCREEN_VIEW>()->
                        getTriangleStepSizeScreen()
                );
            //  m_backgroundCanvas.get<C_CameraPosition2DIso>().pos_ =
            //     offsetScreenToIsoTriangles(
            //         global.systemManager_->get<SCREEN_VIEW>()->
            //             getGlobalCameraOffsetScreen(),
            //         global.systemManager_->get<SCREEN_VIEW>()->
            //             getTriangleStepSizeScreen()
            //     );

            // Write background here for now
            m_backgroundCanvas.get<C_TriangleCanvasBackground>()
                .clearCanvasWithBackground(
                    m_backgroundCanvas.get<C_TriangleCanvasTextures>()
                )
            ;

        }

        virtual void endExecute() override {
            // TODO move to where m_playerCanvas is defined
            if(m_player.id_ != kNullEntityId) {
                m_playerCanvas.get<C_Position3D>().pos_ =
                    m_player.get<C_Position3D>().pos_;
            }
            else {
                m_playerCanvas.get<C_Position3D>().pos_ =
                    vec3(0.0f);
            }
        }
    };


} // namespace IRSystem

#endif /* SYSTEM_RENDERING_SINGLE_VOXEL_TO_CANVAS_H */
