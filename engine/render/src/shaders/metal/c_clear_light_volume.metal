#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_clear_light_volume.glsl. Resets the 128³ light
// volume to zero before the seed pass writes per-light origin texels.
// The parallel winning-light ID read texture (#2318) is cleared to 0 in
// the same pass so an unreached cell reports "no winning light" (id 0).

// Integer mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp —
// the same extent ir_world_lighting.metal publishes as a float for the sampling
// passes (which divide by it to reach texel centers). Deliberately NOT folded
// into that fragment: this kernel needs one integer bound for a loop guard, and
// including it would pull the light-source list + SPOT/ACES helpers into a pass
// that clears two textures. Mirrors the GLSL twin, whose fold would additionally
// declare an unbound binding-4 SSBO here. Keep the two in lockstep.
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
