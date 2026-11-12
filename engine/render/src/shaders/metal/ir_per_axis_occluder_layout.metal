// Mirrors shaders/ir_per_axis_occluder_layout.glsl; keep the two in lockstep.
#ifndef IR_PER_AXIS_OCCLUDER_LAYOUT_METAL_INCLUDED
#define IR_PER_AXIS_OCCLUDER_LAYOUT_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

// Atlas layout of the scatter occluder pyramid, shared by the kernel that
// builds it (c_per_axis_occluder_reduce) and the test that reads it
// (ir_per_axis_occlusion). Mirrors
// IRComponents::PerAxisCanvasStore::occluderLevelSize / occluderLevelOrigin,
// which size the texture.
//
// Level L (1..kOccluderLevels) holds one texel per 2^L-pixel block of the
// raster target; a block that runs past the target covers only the pixels that
// exist. Level 1 sits at the atlas origin and levels 2 and up stack downward
// beside it. A texel is the farthest depth over its block as a code on the
// 2^-24 depth lattice, rounded toward the far plane.

constant int kOccluderLevels = 5;
constant float kOccluderDepthCodes = 16777216.0f;

inline int2 occluderLevelSize(int level, int2 framebufferSize) {
    return (framebufferSize + int2((1 << level) - 1)) >> level;
}

inline int2 occluderLevelOrigin(int level, int2 framebufferSize) {
    if (level == 1) {
        return int2(0);
    }
    int y = 0;
    for (int above = 2; above < level; ++above) {
        y += occluderLevelSize(above, framebufferSize).y;
    }
    return int2(occluderLevelSize(1, framebufferSize).x, y);
}

#endif
