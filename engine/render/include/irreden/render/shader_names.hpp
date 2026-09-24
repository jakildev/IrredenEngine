#ifndef SHADER_NAMES_H
#define SHADER_NAMES_H

namespace IRRender {

const char *const kFileVertFramebufferToScreen = "shaders/v_framebuffer_to_screen.glsl";
const char *const kFileFragFramebufferToScreen = "shaders/f_framebuffer_to_screen.glsl";

const char *const kFileVertSpritesToScreen = "shaders/v_sprites_to_screen.glsl";
const char *const kFileFragSpritesToScreen = "shaders/f_sprites_to_screen.glsl";

const char *const kFileVertTrixelToFramebuffer = "shaders/v_trixel_to_framebuffer.glsl";
const char *const kFileFragTrixelToFramebuffer = "shaders/f_trixel_to_framebuffer.glsl";
const char *const kFileFragTrixelToFramebufferLitShapes =
    "shaders/f_trixel_to_framebuffer_lit_shapes.glsl";
const char *const kFileFragTrixelToFramebufferShapes =
    "shaders/f_trixel_to_framebuffer_shapes.glsl";

// Smooth camera Z-yaw forward-scatter composite: an instanced draw
// over per-axis canvas cells, scattering each occupied cell as its deformed
// face quad into the framebuffer depth buffer (Metal mirror in
// metal/peraxis_scatter.metal).
const char *const kFileVertSourceFaceScatter = "shaders/v_source_face_scatter.glsl";
const char *const kFileFragSourceFaceScatter = "shaders/f_source_face_scatter.glsl";
const char *const kFileVertPerAxisScatter = "shaders/v_peraxis_scatter.glsl";
const char *const kFileFragPerAxisScatter = "shaders/f_peraxis_scatter.glsl";
// The per-axis empty-cell compaction pre-pass scans each canvas's
// distance image and atomic-appends every occupied cell's linear index into a
// per-axis SSBO region + sets the indirect instanced-draw arg count, so the
// scatter composite draws only non-empty cells instead of the full worst-case
// grid. Metal mirror in metal/c_per_axis_cell_compact.metal (add to
// threadgroupSizeForFunctionName).
const char *const kFileCompPerAxisCellCompact = "shaders/c_per_axis_cell_compact.glsl";
// Derives per-axis compute-indirect dispatch dimensions from each axis's
// final occupied count (a cheap 3-thread pass), so the AO / sun-shadow /
// lighting / resolve stages indirect-dispatch over only occupied cells. Split
// out of the compaction kernel to keep that hot full-grid scan barrier-free.
const char *const kFileCompPerAxisCellFinalize = "shaders/c_per_axis_cell_finalize.glsl";

const char *const kFileCompVoxelToTrixelStage1 = "shaders/c_voxel_to_trixel_stage_1.glsl";
// The feeder-pass compile-time specialization (IR_FEEDER_PASS 1) is a second
// program built from the same shared body, so the
// visible stage-1 kernel carries none of the feeder branches (architect a′).
const char *const kFileCompVoxelToTrixelStage1Feeder =
    "shaders/c_voxel_to_trixel_stage_1_feeder.glsl";
// The cardinal winner-election specialization (IR_STORE_WINNER_ELECTION 1)
// re-runs the identical single-canvas geometry
// with every distance tap swapped for a winner-election tap. Dispatched
// between the stage-1 stores and stage 2 (struct 0 only) when the ticking
// pool's storeTiesPossible_ flag is set. Metal mirror in
// metal/c_voxel_to_trixel_stage_1_winner_resolve.metal (registered in
// threadgroupSizeForFunctionName + functionUsesImageAtomicScratch).
const char *const kFileCompVoxelToTrixelStage1WinnerResolve =
    "shaders/c_voxel_to_trixel_stage_1_winner_resolve.glsl";
// Canonical-order the view-visibility overflow entry list between the
// mode-3 append and the overflow indirect draw, so equal-key entries resolve
// their depth contest by record value instead of run-variant append order.
// Dispatched only for pools whose storeTiesPossible_ flag is set — the same
// gate the cardinal winner election takes, CPU-side on both backends.
// Metal mirror in metal/c_per_axis_overflow_sort.metal (registered in
// threadgroupSizeForFunctionName; NOT an image-atomic scratch consumer).
const char *const kFileCompPerAxisOverflowSort = "shaders/c_per_axis_overflow_sort.glsl";
const char *const kFileCompVoxelToTrixelStage2 = "shaders/c_voxel_to_trixel_stage_2.glsl";
// The cardinal winner-guarded specialization makes each
// cardinal colour/entity-id tap additionally require the elected winner to
// match, admitting exactly one of the faces tying a cell's settled distance
// key. Runs in place of the default stage 2 on flagged pools only. Metal
// mirror in metal/c_voxel_to_trixel_stage_2_winner.metal (same registration).
const char *const kFileCompVoxelToTrixelStage2Winner =
    "shaders/c_voxel_to_trixel_stage_2_winner.glsl";
const char *const kFileCompTrixelToTrixel = "shaders/c_trixel_to_trixel.glsl";
const char *const kFileCompTextToTrixel = "shaders/c_text_to_trixel.glsl";
const char *const kFileCompUpdateVoxelPositions = "shaders/c_update_voxel_positions.glsl";
const char *const kFileCompRevoxelizeDetached = "shaders/c_revoxelize_detached.glsl";
const char *const kFileCompShapesToTrixelDepth = "shaders/c_shapes_to_trixel_depth.glsl";
const char *const kFileCompShapesToTrixelPublish = "shaders/c_shapes_to_trixel_publish.glsl";
const char *const kFileCompShapesToTrixelCaster = "shaders/c_shapes_to_trixel_caster.glsl";
const char *const kFileCompShapesToTrixelOwner = "shaders/c_shapes_to_trixel_owner.glsl";
const char *const kFileCompLightingToTrixelShapes = "shaders/c_lighting_to_trixel_shapes.glsl";
const char *const kFileCompLightingToTrixel = "shaders/c_lighting_to_trixel.glsl";
// View-visibility overflow-face lighting relights the overflow entries at their
// world position inside LIGHTING_TO_TRIXEL. Metal mirror in
// metal/c_light_overflow_faces.metal (mapped to a 64×1×1 threadgroup in
// metal_pipeline.cpp).
const char *const kFileCompLightOverflowFaces = "shaders/c_light_overflow_faces.glsl";
const char *const kFileCompFogToTrixel = "shaders/c_fog_to_trixel.glsl";
const char *const kFileCompComputeVoxelAO = "shaders/c_compute_voxel_ao.glsl";
// Hi-Z (max-depth) distance mip-chain build for voxel occlusion culling.
const char *const kFileCompBuildDistanceHiZ = "shaders/c_build_distance_hiz.glsl";
// Chunk-occlusion pre-pass: HZB-tests each pool-chunk's iso AABB against the
// previous frame's Hi-Z and ANDs occluded chunks out of ChunkVisibility.
// Metal mirror in metal/c_chunk_occlusion_cull.metal.
const char *const kFileCompChunkOcclusionCull = "shaders/c_chunk_occlusion_cull.glsl";
// The smooth camera Z-yaw per-axis sun-shadow resolve re-projects the three
// face-local per-axis voxel canvases into a screen-space
// front-most iso-depth scratch SSBO; the blit pass materializes that scratch
// into the resolve R32I texture BAKE_SUN_SHADOW_MAP reads via its cardinal
// path. Metal mirrors in metal/c_resolve_per_axis_screen_depth.metal /
// metal/c_resolve_per_axis_blit.metal.
const char *const kFileCompResolvePerAxisScreenDepth =
    "shaders/c_resolve_per_axis_screen_depth.glsl";
const char *const kFileCompResolvePerAxisBlit = "shaders/c_resolve_per_axis_blit.glsl";
// The world-placed detached re-voxelize sun-shadow cast is a scatter pass that
// re-projects an opt-in detached canvas's model-frame distances into
// the same screen-space scratch layout as the per-axis resolve; reuses
// kFileCompResolvePerAxisBlit for the scratch→texture blit. Metal mirror in
// metal/c_resolve_world_placed_depth.metal.
const char *const kFileCompResolveWorldPlacedDepth = "shaders/c_resolve_world_placed_depth.glsl";
const char *const kFileCompClearSunShadowMap = "shaders/c_clear_sun_shadow_map.glsl";
const char *const kFileCompBakeVoxelSunFaces = "shaders/c_bake_voxel_sun_faces.glsl";
const char *const kFileCompBakeBoxSunShadow = "shaders/c_bake_box_sun_shadow.glsl";
const char *const kFileCompBakeSunShadowMap = "shaders/c_bake_sun_shadow_map.glsl";
const char *const kFileCompComputeSunShadow = "shaders/c_compute_sun_shadow.glsl";
const char *const kFileCompComputeSunShadowShapes = "shaders/c_compute_sun_shadow_shapes.glsl";
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
