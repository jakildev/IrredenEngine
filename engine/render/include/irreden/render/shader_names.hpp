#ifndef SHADER_NAMES_H
#define SHADER_NAMES_H

namespace IRRender {

const char *const kFileVertFramebufferToScreen = "shaders/v_framebuffer_to_screen.glsl";
const char *const kFileFragFramebufferToScreen = "shaders/f_framebuffer_to_screen.glsl";

const char *const kFileVertSpritesToScreen = "shaders/v_sprites_to_screen.glsl";
const char *const kFileFragSpritesToScreen = "shaders/f_sprites_to_screen.glsl";

const char *const kFileVertTrixelToFramebuffer = "shaders/v_trixel_to_framebuffer.glsl";
const char *const kFileFragTrixelToFramebuffer = "shaders/f_trixel_to_framebuffer.glsl";

// Smooth camera Z-yaw forward-scatter composite (T3 / #1310): instanced draw
// over per-axis canvas cells, scattering each occupied cell as its deformed
// face quad into the framebuffer depth buffer (Metal mirror in
// metal/peraxis_scatter.metal).
const char *const kFileVertPerAxisScatter = "shaders/v_peraxis_scatter.glsl";
const char *const kFileFragPerAxisScatter = "shaders/f_peraxis_scatter.glsl";

// Detached-entity SO(3) forward-scatter composite (P3b / #1475): the
// per-DETACHED-entity analog of the camera-yaw scatter above. Same instanced
// per-axis-cell draw, but projects each cell under the entity's octahedral-snap
// residual quaternion (pos3DtoPos2DIsoRotated) and places it at the entity's
// iso screen position. Reuses f_peraxis_scatter for the fragment stage (Metal
// mirror in metal/peraxis_scatter_detached.metal).
const char *const kFileVertPerAxisScatterDetached = "shaders/v_peraxis_scatter_detached.glsl";

const char *const kFileCompVoxelToTrixelStage1 = "shaders/c_voxel_to_trixel_stage_1.glsl";
const char *const kFileCompVoxelToTrixelStage2 = "shaders/c_voxel_to_trixel_stage_2.glsl";
const char *const kFileCompTrixelToTrixel = "shaders/c_trixel_to_trixel.glsl";
const char *const kFileCompTextToTrixel = "shaders/c_text_to_trixel.glsl";
const char *const kFileCompUpdateVoxelPositions = "shaders/c_update_voxel_positions.glsl";
const char *const kFileCompShapesToTrixel = "shaders/c_shapes_to_trixel.glsl";
const char *const kFileCompLightingToTrixel = "shaders/c_lighting_to_trixel.glsl";
const char *const kFileCompFogToTrixel = "shaders/c_fog_to_trixel.glsl";
const char *const kFileCompComputeVoxelAO = "shaders/c_compute_voxel_ao.glsl";
// Smooth camera Z-yaw per-axis sun-shadow resolve (#1435): the scatter pass
// re-projects the three face-local per-axis voxel canvases into a screen-space
// front-most iso-depth scratch SSBO; the blit pass materializes that scratch
// into the resolve R32I texture BAKE_SUN_SHADOW_MAP reads via its cardinal
// path. Metal mirrors in metal/c_resolve_per_axis_screen_depth.metal /
// metal/c_resolve_per_axis_blit.metal.
const char *const kFileCompResolvePerAxisScreenDepth =
    "shaders/c_resolve_per_axis_screen_depth.glsl";
const char *const kFileCompResolvePerAxisBlit = "shaders/c_resolve_per_axis_blit.glsl";
const char *const kFileCompClearSunShadowMap = "shaders/c_clear_sun_shadow_map.glsl";
const char *const kFileCompBakeSunShadowMap = "shaders/c_bake_sun_shadow_map.glsl";
const char *const kFileCompComputeSunShadow = "shaders/c_compute_sun_shadow.glsl";
const char *const kFileCompVoxelVisibilityCompact = "shaders/c_voxel_visibility_compact.glsl";
const char *const kFileCompClearLightVolume = "shaders/c_clear_light_volume.glsl";
const char *const kFileCompSeedLightVolume = "shaders/c_seed_light_volume.glsl";
const char *const kFileCompPropagateLightVolume = "shaders/c_propagate_light_volume.glsl";
const char *const kFileCompUpdateGpuParticles = "shaders/c_update_gpu_particles.glsl";
const char *const kFileCompRenderGpuParticlesToTrixel =
    "shaders/c_render_gpu_particles_to_trixel.glsl";
const char *const kFileCompRenderStatelessParticlesToTrixel =
    "shaders/c_render_stateless_particles_to_trixel.glsl";

const char *const kFileVertDebugOverlay = "shaders/v_debug_overlay.glsl";
const char *const kFileFragDebugOverlay = "shaders/f_debug_overlay.glsl";

} // namespace IRRender

#endif /* SHADER_NAMES_H */
