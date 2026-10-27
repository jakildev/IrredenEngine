#version 450 core

#include "ir_glitch_common.glsl"

// Covers the pass's output image, the main framebuffer's twin, texel for texel
// (`framebufferSelfMvp`); v_framebuffer_to_screen then places that image on
// screen as it would the framebuffer itself.

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoords;

// Main-framebuffer texel coordinates. Derived here from the colour's size so
// the vertex stage reads the composite too: a pass whose vertex stage reads no
// texture can start before the composite finishes, and its GPU timestamp then
// includes that wait.
out vec2 TexelCoords;
// Centre of the depth effect's interval: the first non-background depth
// among the probes, or 1.0 when every probe is background. Uniform across
// the pass, so the probe scan runs per vertex rather than per fragment.
flat out float DepthCenter;

layout (binding = 0) uniform sampler2D mainColor;
layout (binding = 1) uniform sampler2D mainDepth;

layout (std140, binding = 2) uniform FrameDataFramebufferGlitch {
    mat4 mvpMatrix;
    // Period tick low/high words, then hold tick low/high words.
    uvec4 tickWords;
    // seed, cell threshold, depth probe start, depth enabled.
    uvec4 hashInputs;
    // Framebuffer pixels per trixel (xy) and per cell (zw).
    ivec4 trixelStep;
    // Band counts: tear, hold, cells; then the bar count.
    ivec4 counts;
    // Split and depth-tear x offsets in framebuffer pixels.
    ivec4 offsets;
    // Depth-hold blend, depth half-width (normalized depth), bar strength.
    vec4 strengths;
    // Rows [x, y) of each band; tear bands carry their x shift in z.
    ivec4 tearBands[6];
    ivec4 holdBands[6];
    ivec4 cellBands[6];
    ivec4 barRows;
    ivec4 barColours;
};

void main() {
    ivec2 size = textureSize(mainColor, 0);
    TexelCoords = aTexCoords * vec2(size);
    DepthCenter = 1.0;
    if (hashInputs.w != 0u) {
        for (int i = 0; i < kGlitchDepthProbeCount; ++i) {
            int probe = (int(hashInputs.z) + i) % kGlitchDepthProbeCount;
            float probeDepth = texelFetch(mainDepth, glitchDepthProbePixel(probe, size), 0).r;
            if (probeDepth < 1.0) {
                DepthCenter = probeDepth;
                break;
            }
        }
    }
    gl_Position = mvpMatrix * vec4(aPos, 1.0, 1.0);
}
