#version 450 core

// Resets the 128³ light-volume read texture to (0,0,0,0) before the seed
// pass writes per-light origin texels and the propagate pass dilates
// outward. Without this clear stale luminance from the previous frame
// would persist as ghost lights.
//
// Dispatched as a compute pass to avoid an 8 MiB CPU upload per frame
// (the legacy CPU producer's hot path before issue #359). 8³ workgroups
// over a 128³ volume yield 16 × 16 × 16 dispatch groups.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 8) in;

const int kLightVolumeSize = 128;

layout(rgba8, binding = 0) writeonly uniform image3D lightVolume;
// Winning-light ID channel (#2318): reset to 0 ("no light") in lockstep
// with the color volume so a stale ID can't survive into this frame.
layout(r16ui, binding = 1) writeonly uniform uimage3D lightVolumeId;

void main() {
    ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (cell.x >= kLightVolumeSize ||
        cell.y >= kLightVolumeSize ||
        cell.z >= kLightVolumeSize) {
        return;
    }
    imageStore(lightVolume, cell, vec4(0.0));
    imageStore(lightVolumeId, cell, uvec4(0u));
}
