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

out vec2 TexCoords;

layout (binding = 0) uniform sampler2D triangleColors;
layout (binding = 1) uniform isampler2D  triangleDistances;

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
    // (Linux/macOS) accepted the vertex-side head-only form; the stricter
    // native-Windows driver rejects it with "members of uniform block (named
    // FrameDataIsoTriangles) are not the same between shader stages" and the
    // program-link assert crashes every demo at startup. The vertex stage
    // consumes only mpMatrix + textureOffset, but it must still declare the
    // full scatter UBO tail so the std140 layout matches BOTH the fragment
    // stage and the shared C++ struct (FrameDataTrixelToFramebuffer,
    // ir_render_types.hpp). Keep these two blocks in lockstep.
    ivec2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    ivec4 visibleFaceIds;
    vec4 _detachedResidualPad;
    vec4 _detachedDepthAxisPad;
    vec4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    // No-priority perf fast-path flag (#2155); repurposes the former _depthColorPad0
    // slot at offset 200. This vertex stage doesn't read it — declared only to keep
    // FrameDataIsoTriangles member-identical to the fragment stage (see the lockstep
    // note above) and to the C++ FrameDataTrixelToFramebuffer.
    int anyPerTrixelPriority;
    int depthPriorityMode;
};

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    // -aPos.y flips the texture V so that canvas pixel Y=0 (GL bottom)
    // appears at the top of the screen.  This means higher iso/canvas Y
    // renders lower on the final output.
    TexCoords = vec2(aPos.x, -aPos.y) + 0.50 + (textureOffset / vec2(textureSize));
    gl_Position = mpMatrix * vec4(aPos, 1.0f, 1.0f);
}
