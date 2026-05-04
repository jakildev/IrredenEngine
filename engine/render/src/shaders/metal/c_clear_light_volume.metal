#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_clear_light_volume.glsl. Resets the 128³ light
// volume to zero before the seed pass writes per-light origin texels.

constant int kLightVolumeSize = 128;

kernel void c_clear_light_volume(
    texture3d<float, access::write> lightVolume [[texture(0)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int3 cell = int3(globalId);
    if (cell.x >= kLightVolumeSize ||
        cell.y >= kLightVolumeSize ||
        cell.z >= kLightVolumeSize) {
        return;
    }
    lightVolume.write(float4(0.0), uint3(cell));
}
