/*
 * Project: Irreden Engine
 * File: v_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 450 core
layout (location = 0) in vec2 aPos;

layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    // This block is ALSO declared in f_trixel_to_framebuffer.glsl, and a
    // uniform block shared by name across stages MUST be member-identical or
    // the GL spec makes the program link ill-formed. Lenient drivers
    // (Linux/macOS) accept a vertex-side head-only form; the stricter
    // native-Windows driver rejects it with "members of uniform block (named
    // FrameDataIsoTriangles) are not the same between shader stages" and the
    // program link fails. The vertex stage consumes only mpMatrix, but it
    // must still declare the full block tail so
    // the std140 layout matches BOTH the fragment stage and the shared C++
    // struct (FrameDataTrixelToFramebuffer, ir_render_types.hpp). Keep these
    // two blocks in lockstep.
    ivec2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    ivec4 visibleFaceIds;
    vec4 _detachedResidualPad;
    vec4 casterViewToWorld;
    vec4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    int anyPerTrixelPriority;
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
    uint fogBodyFactorEncoded;
    uint fogUnexploredColorPacked;
    ivec4 perAxisStoreFrame;
    vec4 canvasFromPixel;
};

void main() {
    // The fragment stage derives its canvas position from gl_FragCoord
    // (canvasPositionFromPixel); nothing is interpolated.
    gl_Position = mpMatrix * vec4(aPos, 1.0f, 1.0f);
}
