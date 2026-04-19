#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_lighting_to_trixel.glsl. Screen-space lighting
// application pass — modulates trixelColors.rgb by the per-pixel AO
// factor written to canvasAO.r by COMPUTE_VOXEL_AO. When lutEnabled is
// set, the plain AO multiply is replaced by a luminance-indexed LUT
// lookup whose X-axis is the AO value.

struct FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    float debugLightLevel;
    int   _padding2;
};

kernel void c_lighting_to_trixel(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasAO [[texture(2)]],
    texture2d<float, access::sample> paletteLUT [[texture(3)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    if (frameData.lightingEnabled == 0) {
        return;
    }

    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int distance = trixelDistances.read(uint2(pixel)).x;
    if (distance >= 65535) {
        return;
    }

    const float  ao  = canvasAO.read(uint2(pixel)).r;
    const float4 src = trixelColors.read(uint2(pixel));

    if (frameData.lutEnabled == 0) {
        trixelColors.write(float4(src.rgb * ao, src.a), uint2(pixel));
        return;
    }

    // LUT palette shading: AO drives the X axis (light level), luminance
    // drives Y. The LUT encodes both the brightness curve and the shadow
    // tint, so the plain AO multiply is skipped.
    constexpr sampler s(filter::nearest, address::clamp_to_edge);
    const float  luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
    const float4 lut       = paletteLUT.sample(s, float2(ao, luminance));
    trixelColors.write(float4(src.rgb * lut.rgb, src.a), uint2(pixel));
}
