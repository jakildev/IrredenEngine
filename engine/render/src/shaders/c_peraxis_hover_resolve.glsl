#version 450 core

#include "ir_pick_record.glsl"
layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std140, binding = 3) uniform FrameDataIsoTriangles {
    vec4 compositePrefix[16];
    ivec2 mouseFramebufferPixel;
    int hoverResolveMode;
    int gatherFilterDiagnostic;
    vec4 cameraViewRotation;
};
layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    PickRecord pick;
};
layout(binding = 0) uniform usampler2D entityIdAttachment;
layout(binding = 1) uniform sampler2D compositeDepth;

void main() {
    const ivec2 size = textureSize(compositeDepth, 0);
    const ivec2 pixel = ivec2(mouseFramebufferPixel.x, size.y - 1 - mouseFramebufferPixel.y);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) return;
    const float depth = texelFetch(compositeDepth, pixel, 0).r;
    const uvec2 id = hoverResolveMode == 0 ? texelFetch(entityIdAttachment, pixel, 0).rg : uvec2(0);
    scatterPick(pick, id, depth, hoverResolveMode);
}
