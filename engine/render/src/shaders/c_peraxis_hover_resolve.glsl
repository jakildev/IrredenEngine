#version 450 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;
layout(std140, binding = 3) uniform FrameDataIsoTriangles {
    vec4 compositePrefix[16];
    ivec2 mouseFramebufferPixel;
    int hoverResolveMode;
    int hoverResolvePad;
};
layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    uvec2 hoveredEntityId;
    float hoveredDepth;
    float pad;
    uvec2 scatterEntityId;
    float scatterDepth;
    uint scatterOwner;
};
layout(binding = 0) uniform usampler2D entityIdAttachment;
layout(binding = 1) uniform sampler2D compositeDepth;

void main() {
    const ivec2 size = textureSize(compositeDepth, 0);
    if (any(lessThan(mouseFramebufferPixel, ivec2(0))) ||
        any(greaterThanEqual(mouseFramebufferPixel, size))) return;
    const ivec2 pixel = ivec2(mouseFramebufferPixel.x, size.y - 1 - mouseFramebufferPixel.y);
    const float depth = texelFetch(compositeDepth, pixel, 0).r;
    if (hoverResolveMode == 0) {
        const uvec2 id = texelFetch(entityIdAttachment, pixel, 0).rg;
        scatterOwner = any(notEqual(id, uvec2(0))) ? 1u : 0u;
        scatterEntityId = id;
        scatterDepth = depth;
    } else if (scatterOwner != 0u && floatBitsToUint(depth) == floatBitsToUint(scatterDepth)) {
        hoveredEntityId = scatterEntityId;
        hoveredDepth = depth;
    }
}
