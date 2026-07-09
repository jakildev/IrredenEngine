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
// Per-axis empty-cell compaction pre-pass (#1961): scans each per-axis canvas's
// distance image and atomic-appends every occupied cell's linear index into a
// per-axis SSBO region + sets the indirect instanced-draw arg count, so the
// scatter composite draws only non-empty cells instead of the full worst-case
// grid. Metal mirror in metal/c_per_axis_cell_compact.metal (add to
// threadgroupSizeForFunctionName).
const char *const kFileCompPerAxisCellCompact = "shaders/c_per_axis_cell_compact.glsl";
// #2256: derives the per-axis compute-indirect dispatch dims from each axis's
// final occupied count (a cheap 3-thread pass), so the AO / sun-shadow /
// lighting / resolve stages indirect-dispatch over only occupied cells. Split
// out of the compaction kernel to keep that hot full-grid scan barrier-free.
const char *const kFileCompPerAxisCellFinalize = "shaders/c_per_axis_cell_finalize.glsl";

const char *const kFileCompVoxelToTrixelStage1 = "shaders/c_voxel_to_trixel_stage_1.glsl";
// #2258 Step B: the feeder-pass compile-time specialization of stage 1
// (IR_FEEDER_PASS 1) — a second program built from the same shared body, so the
// visible stage-1 kernel carries none of the feeder branches (architect a′).
const char *const kFileCompVoxelToTrixelStage1Feeder =
    "shaders/c_voxel_to_trixel_stage_1_feeder.glsl";
const char *const kFileCompVoxelToTrixelStage2 = "shaders/c_voxel_to_trixel_stage_2.glsl";
const char *const kFileCompTrixelToTrixel = "shaders/c_trixel_to_trixel.glsl";
const char *const kFileCompTextToTrixel = "shaders/c_text_to_trixel.glsl";
const char *const kFileCompUpdateVoxelPositions = "shaders/c_update_voxel_positions.glsl";
const char *const kFileCompRevoxelizeDetached = "shaders/c_revoxelize_detached.glsl";
const char *const kFileCompShapesToTrixel = "shaders/c_shapes_to_trixel.glsl";
const char *const kFileCompLightingToTrixel = "shaders/c_lighting_to_trixel.glsl";
const char *const kFileCompFogToTrixel = "shaders/c_fog_to_trixel.glsl";
const char *const kFileCompComputeVoxelAO = "shaders/c_compute_voxel_ao.glsl";
// Hi-Z (max-depth) distance mip-chain build for voxel occlusion culling
// (#1294 child 1/3). Metal mirror in metal/c_build_distance_hiz.metal.
const char *const kFileCompBuildDistanceHiZ = "shaders/c_build_distance_hiz.glsl";
// Chunk-occlusion pre-pass: HZB-tests each pool-chunk's iso AABB against last
// frame's Hi-Z and ANDs occluded chunks out of ChunkVisibility (#1294 child
// 2/3). Metal mirror in metal/c_chunk_occlusion_cull.metal.
const char *const kFileCompChunkOcclusionCull = "shaders/c_chunk_occlusion_cull.glsl";
// Smooth camera Z-yaw per-axis sun-shadow resolve (#1435): the scatter pass
// re-projects the three face-local per-axis voxel canvases into a screen-space
// front-most iso-depth scratch SSBO; the blit pass materializes that scratch
// into the resolve R32I texture BAKE_SUN_SHADOW_MAP reads via its cardinal
// path. Metal mirrors in metal/c_resolve_per_axis_screen_depth.metal /
// metal/c_resolve_per_axis_blit.metal.
const char *const kFileCompResolvePerAxisScreenDepth =
    "shaders/c_resolve_per_axis_screen_depth.glsl";
const char *const kFileCompResolvePerAxisBlit = "shaders/c_resolve_per_axis_blit.glsl";
// World-placed detached re-voxelize sun-shadow cast (#1576 P4b-3): scatter
// pass re-projecting an opt-in detached canvas's model-frame distances into
// the same screen-space scratch layout as the per-axis resolve; reuses
// kFileCompResolvePerAxisBlit for the scratch→texture blit. Metal mirror in
// metal/c_resolve_world_placed_depth.metal.
const char *const kFileCompResolveWorldPlacedDepth = "shaders/c_resolve_world_placed_depth.glsl";
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
