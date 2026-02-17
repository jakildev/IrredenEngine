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

} // namespace IRRender

#endif /* SHADER_NAMES_H */
