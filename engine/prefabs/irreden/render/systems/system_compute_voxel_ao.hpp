#ifndef SYSTEM_COMPUTE_VOXEL_AO_H
#define SYSTEM_COMPUTE_VOXEL_AO_H

// Per-pixel ambient-occlusion compute pass. Reads the canvas distance
// texture and the 3D occupancy grid SSBO, samples edge-adjacent neighbors
// for each face pixel, and writes a grayscale AO factor to the canvas AO
// texture.
//
// Pipeline order constraint: must run after all geometry stages
// (VOXEL_TO_TRIXEL_STAGE_2, SHAPES_TO_TRIXEL) so the distance texture is
// fully populated, after BUILD_OCCUPANCY_GRID so the SSBO is current, and
// before LIGHTING_TO_TRIXEL which consumes the AO texture.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/gpu_stage_timing.hpp>

#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_voxel_ao.glsl.
constexpr int kComputeVoxelAOGroupSize = 16;

template <> struct System<COMPUTE_VOXEL_AO> {
    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeVoxelAOProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompComputeVoxelAO, ShaderType::COMPUTE}
            }
        );

        static ShaderProgram *s_program =
            IRRender::getNamedResource<ShaderProgram>("ComputeVoxelAOProgram");
        static Buffer *s_occupancySSBO =
            IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        static Buffer *s_voxelFrameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        // `ComputeSunShadowFrameData` is created by the COMPUTE_SUN_SHADOW
        // system, which is constructed AFTER AO in pipeline registration
        // order. Looking it up here (during AO's create()) would race the
        // creation. Looking it up inside the lambdas below defers the
        // resolution to first-tick, by which time every system's create()
        // has run. AO consumes only `aoEnabled_` and refreshes that
        // single field in its beginTick (see below) so the value is fresh
        // at AO-dispatch time even though shadow hasn't ticked yet this
        // frame.

        return createSystem<
            C_TriangleCanvasTextures,
            C_CanvasAOTexture,
            C_TrixelCanvasRenderBehavior
        >(
            "ComputeVoxelAO",
            [](const C_TriangleCanvasTextures &canvasTextures,
               const C_CanvasAOTexture &ao,
               const C_TrixelCanvasRenderBehavior &behavior) {
                // GUI canvases carry no world-space geometry, so AO would
                // read garbage iso coords — skip.
                if (!behavior.useCameraPositionIso_) return;
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0;
                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }

                canvasTextures.getTextureDistances()->bindAsImage(
                    0, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                ao.getTexture()->bindAsImage(
                    1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                s_occupancySSBO->bindBase(
                    BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid
                );
                s_voxelFrameDataBuf->bindBase(
                    BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas
                );
                static Buffer *s_sunFrameDataBuf =
                    IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
                s_sunFrameDataBuf->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

                const int groupsX = IRMath::divCeil(
                    canvasTextures.size_.x, kComputeVoxelAOGroupSize
                );
                const int groupsY = IRMath::divCeil(
                    canvasTextures.size_.y, kComputeVoxelAOGroupSize
                );
                IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                // Assumes a single matching canvas per frame. Switch to `+=`
                // with a `beginTick` reset if the filter ever matches
                // multiple entities — otherwise later entities overwrite.
                if (timing.enabled_) { IRRender::device()->finish(); timing.computeVoxelAoMs_ = IRRender::elapsedMs(t0, IRRender::SteadyClock::now()); }
            },
            []() {
                s_program->use();
                // Refresh just `aoEnabled_` in the shared FrameDataSun
                // buffer so the AO shader reads a current value. The
                // COMPUTE_SUN_SHADOW system later overwrites the entire
                // struct with the same field derived from
                // IRRender::getAOEnabled(), so the two writes agree.
                static Buffer *s_sunFrameDataBuf =
                    IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
                int aoEnabledFlag = IRRender::getAOEnabled() ? 1 : 0;
                s_sunFrameDataBuf->subData(
                    offsetof(FrameDataSun, aoEnabled_),
                    sizeof(int),
                    &aoEnabledFlag
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_VOXEL_AO_H */
