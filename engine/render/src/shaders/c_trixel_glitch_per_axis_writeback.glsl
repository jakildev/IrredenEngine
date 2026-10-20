#version 450 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std430, binding = 27) buffer TrixelGlitchTileFlags {
    uint tileFlags[];
};
layout(std430, binding = 28) readonly buffer TrixelGlitchTileList {
    uint tileList[];
};

layout(rgba8, binding = 0) writeonly uniform image2D liveColors;
layout(rg32ui, binding = 2) readonly uniform uimage2D liveEntityIds;
layout(rgba8, binding = 5) readonly uniform image2D scratchColors;

const int kTileSize = 16;

void main() {
    ivec2 size = imageSize(liveColors);
    ivec2 tileGrid = (size + ivec2(kTileSize - 1)) / kTileSize;
    uint tile = tileList[gl_WorkGroupID.x];
    ivec2 tileOrigin = ivec2(int(tile % uint(tileGrid.x)), int(tile / uint(tileGrid.x))) *
                       kTileSize;
    ivec2 p = tileOrigin + ivec2(gl_LocalInvocationID.xy);
    if (all(lessThan(p, size))) {
        uvec2 id = decodeEntityId(imageLoad(liveEntityIds, p).xy);
        if (any(notEqual(id, uvec2(0u)))) {
            imageStore(liveColors, p, imageLoad(scratchColors, p));
        }
    }
    barrier();
    if (gl_LocalInvocationIndex == 0u) {
        atomicExchange(tileFlags[tile], 0u);
    }
}
