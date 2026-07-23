#version 450 core

// Resets the 128³ light-volume read texture to (0,0,0,0) before the seed
// pass writes per-light origin texels and the propagate pass dilates
// outward. Without this clear stale luminance from the previous frame
// would persist as ghost lights. The parallel winning-light ID read
// texture (#2318) is cleared to 0 in the same pass so a cell with no
// reaching light reports "no winning light" (id 0).
//
// Dispatched as a compute pass to avoid an 8 MiB CPU upload per frame
// (the legacy CPU producer's hot path before issue #359). 8³ workgroups
// over a 128³ volume yield 16 × 16 × 16 dispatch groups.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

// Integer mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp —
// the same extent ir_world_lighting.glsl publishes as a float for the sampling
// passes (which divide by it to reach texel centers). Deliberately NOT folded
// into that fragment: this kernel needs one integer bound for a loop guard, and
// including ir_world_lighting.glsl would declare its binding-4 LightSourceBuffer
// SSBO in a clear pass that binds nothing at slot 4. Keep the two in lockstep.
const int kLightVolumeSize = 128;

layout(rgba8, binding = 0) writeonly uniform image3D lightVolume;
// Winning-light ID read texture (#2318): `.r` = lightIndex/255, 0 = none.
layout(rgba8, binding = 1) writeonly uniform image3D lightVolumeId;

void main() {
    ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (cell.x >= kLightVolumeSize ||
        cell.y >= kLightVolumeSize ||
        cell.z >= kLightVolumeSize) {
        return;
    }
    imageStore(lightVolume, cell, vec4(0.0));
    imageStore(lightVolumeId, cell, vec4(0.0));
}
