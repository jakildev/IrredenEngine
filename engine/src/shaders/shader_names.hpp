/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\shaders\shader_names.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SHADER_NAMES_H
#define SHADER_NAMES_H

namespace IRShaders {

    const char* const kFileVertFramebufferScreen =
        "shaders/v_framebuffer_screen.glsl";
    const char* const kFileFragFramebufferScreen =
        "shaders/f_framebuffer_screen.glsl";

    const char* const kFileVertIsoTrianglesScreen =
        "shaders/v_iso_triangle_screen.glsl";
    const char* const kFileFragIsoTrianglesScreen =
        "shaders/f_iso_triangle_screen.glsl";

    const char* const kFileCompSingleVoxelToIsoTriangleScreen =
        "shaders/c_single_voxel_to_canvas.glsl";
    const char* const kFileCompSingleVoxelToCanvasSecondPass =
        "shaders/c_single_voxel_stage_2_color.glsl";

}

#endif /* SHADER_NAMES_H */
