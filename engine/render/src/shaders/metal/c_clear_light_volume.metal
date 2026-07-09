#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_clear_light_volume.glsl. Resets the 128³ light
// volume to zero before the seed pass writes per-light origin texels.

constant int kLightVolumeSize = 128;

kernel void c_clear_light_volume(
    texture3d<float, access::write> lightVolume [[texture(0)]],
    // Winning-light ID channel (#2318): reset to 0 ("no light") in lockstep
    // with the color volume so a stale ID can't survive into this frame.
    texture3d<uint, access::write> lightVolumeId [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int3 cell = int3(globalId);
    if (cell.x >= kLightVolumeSize ||
        cell.y >= kLightVolumeSize ||
        cell.z >= kLightVolumeSize) {
        return;
    }
    lightVolume.write(float4(0.0), uint3(cell));
    lightVolumeId.write(uint4(0), uint3(cell));
}
