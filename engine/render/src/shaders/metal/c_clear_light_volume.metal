#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_clear_light_volume.glsl. Winning-light id 0 means "no
// winning light", so the id texture clears to 0 alongside the volume.

// Integer mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp —
// the same extent ir_world_lighting.metal publishes as a float for the sampling
// passes (which divide by it to reach texel centers). Not taken from that
// fragment, mirroring the GLSL twin, where including it would declare an
// unbound binding-4 SSBO. Keep the two in lockstep.
constant int kLightVolumeSize = 128;

kernel void c_clear_light_volume(
    texture3d<float, access::write> lightVolume [[texture(0)]],
    texture3d<float, access::write> lightVolumeId [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int3 cell = int3(globalId);
    if (cell.x >= kLightVolumeSize ||
        cell.y >= kLightVolumeSize ||
        cell.z >= kLightVolumeSize) {
        return;
    }
    lightVolume.write(float4(0.0), uint3(cell));
    lightVolumeId.write(float4(0.0), uint3(cell));
}
