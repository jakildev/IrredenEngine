#ifndef SYSTEM_COMPUTE_VOXEL_AO_H
#define SYSTEM_COMPUTE_VOXEL_AO_H

// Per-pixel ambient-occlusion compute pass. Reads the canvas distance
// texture, samples four face-tangent neighbour pixels for each face
// pixel, and writes a grayscale AO factor to the canvas AO texture.
//
// Pipeline order constraint: must run after all geometry stages
// (VOXEL_TO_TRIXEL_STAGE_2, SHAPES_TO_TRIXEL) so the distance texture is
// fully populated, and before LIGHTING_TO_TRIXEL which consumes the AO
// texture.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_compute_voxel_ao.glsl.
constexpr int kComputeVoxelAOGroupSize = 16;

template <> struct System<COMPUTE_VOXEL_AO> {
    ShaderProgram *program_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    // `ComputeSunShadowFrameData` is created by COMPUTE_SUN_SHADOW,
    // which is constructed AFTER AO in pipeline registration order.
    // Resolved lazily on the first beginTick (which fires before any
    // per-entity tick), so the per-entity tick can use it directly.
    Buffer *sunFrameDataBuf_ = nullptr;

    void tick(
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasAOTexture &ao,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // GUI canvases carry no world-space geometry, so AO would
        // read garbage iso coords — skip.
        if (!behavior.useCameraPositionIso_)
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        canvasTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        ao.getTexture()->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);

        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kComputeVoxelAOGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kComputeVoxelAOGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
    }

    void beginTick() {
        program_->use();
        // AO only writes `aoEnabled_` into the shared FrameDataSun
        // buffer. All other fields carry the previous frame's values
        // written by BAKE_SUN_SHADOW_MAP's tick — AO must not read
        // them. BAKE_SUN_SHADOW_MAP's tick is the authoritative
        // writer for the full struct (subData(0, sizeof(FrameDataSun),
        // &frameData_) after its beginTick populates frameData_); AO
        // runs before it in pipeline order, so this partial write is
        // safe and the two systems agree on `aoEnabled_` via the same
        // IRRender::getAOEnabled() source.
        if (sunFrameDataBuf_ == nullptr) {
            sunFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }
        int aoEnabledFlag = IRRender::getAOEnabled() ? 1 : 0;
        sunFrameDataBuf_->subData(offsetof(FrameDataSun, aoEnabled_), sizeof(int), &aoEnabledFlag);
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ComputeVoxelAOProgram",
            std::vector{ShaderStage{IRRender::kFileCompComputeVoxelAO, ShaderType::COMPUTE}}
        );

        SystemId systemId = registerSystem<
            COMPUTE_VOXEL_AO,
            C_TriangleCanvasTextures,
            C_CanvasAOTexture,
            C_TrixelCanvasRenderBehavior>("ComputeVoxelAO");
        auto *p = getSystemParams<System<COMPUTE_VOXEL_AO>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("ComputeVoxelAOProgram");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "computeVoxelAO");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_COMPUTE_VOXEL_AO_H */
