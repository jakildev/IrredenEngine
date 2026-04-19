#ifndef SHADER_NAMES_H
#define SHADER_NAMES_H

namespace IRRender {

const char *const kFileVertFramebufferToScreen = "shaders/v_framebuffer_to_screen.glsl";
const char *const kFileFragFramebufferToScreen = "shaders/f_framebuffer_to_screen.glsl";

const char *const kFileVertTrixelToFramebuffer = "shaders/v_trixel_to_framebuffer.glsl";
const char *const kFileFragTrixelToFramebuffer = "shaders/f_trixel_to_framebuffer.glsl";

const char *const kFileCompVoxelToTrixelStage1 = "shaders/c_voxel_to_trixel_stage_1.glsl";
const char *const kFileCompVoxelToTrixelStage2 = "shaders/c_voxel_to_trixel_stage_2.glsl";
const char *const kFileCompTrixelToTrixel = "shaders/c_trixel_to_trixel.glsl";
const char *const kFileCompTextToTrixel = "shaders/c_text_to_trixel.glsl";
const char *const kFileCompUpdateVoxelPositions = "shaders/c_update_voxel_positions.glsl";
const char *const kFileCompShapesToTrixel = "shaders/c_shapes_to_trixel.glsl";
const char *const kFileCompLightingToTrixel = "shaders/c_lighting_to_trixel.glsl";
const char *const kFileCompComputeVoxelAO = "shaders/c_compute_voxel_ao.glsl";
const char *const kFileCompComputeSunShadow = "shaders/c_compute_sun_shadow.glsl";
const char *const kFileCompVoxelVisibilityCompact = "shaders/c_voxel_visibility_compact.glsl";

const char *const kFileVertDebugOverlay = "shaders/v_debug_overlay.glsl";
const char *const kFileFragDebugOverlay = "shaders/f_debug_overlay.glsl";

} // namespace IRRender

#endif /* SHADER_NAMES_H */
