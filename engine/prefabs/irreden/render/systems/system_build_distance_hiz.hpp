#ifndef SYSTEM_BUILD_DISTANCE_HIZ_H
#define SYSTEM_BUILD_DISTANCE_HIZ_H

// Explicit final-distance pyramid build for existing pipeline registrations.
// The voxel raster builds its own current-frame pyramid before election and
// never uses this later pass as a temporal proof source. It is retained for
// pipelines that consume final distances after shapes and AO.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/occlusion_lag_source.hpp>
#include <irreden/render/distance_hiz.hpp>

#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Matches local_size_{x,y} in c_build_distance_hiz.glsl (and the Metal
// threadgroup size registered in metal_pipeline.cpp).
constexpr int kBuildDistanceHiZGroupSize = 16;

template <> struct System<COMPUTE_DISTANCE_HIZ> {
    ShaderProgram *program_ = nullptr;
    // Resolved in beginTick from the occlusion-cull flag.
    bool build_ = false;

    void tick(
        const C_TriangleCanvasTextures &canvasTextures, const C_TrixelCanvasRenderBehavior &behavior
    ) {
        // GUI canvases carry no world-space geometry and never feed the voxel
        // occlusion cull, so a Hi-Z over their distances is wasted work — skip
        // (mirrors COMPUTE_VOXEL_AO's guard). A 1x1 canvas yields no levels.
        if (!build_ || !behavior.useCameraPositionIso_)
            return;
        if (canvasTextures.hiZMips_.empty())
            return;
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        IRPrefab::OcclusionCull::detail::buildDistanceHiZ(canvasTextures, *program_);
    }

    void beginTick() {
        build_ =
            IRPrefab::OcclusionCull::buildsDistanceHiZ(IRRender::getVoxelOcclusionCullEnabled());
        if (build_) {
            program_->use();
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "BuildDistanceHiZProgram",
            std::vector{ShaderStage{IRRender::kFileCompBuildDistanceHiZ, ShaderType::COMPUTE}}
        );

        SystemId systemId = registerSystem<
            COMPUTE_DISTANCE_HIZ,
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior>("BuildDistanceHiZ");
        auto *p = getSystemParams<System<COMPUTE_DISTANCE_HIZ>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("BuildDistanceHiZProgram");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_DISTANCE_HIZ_H */
