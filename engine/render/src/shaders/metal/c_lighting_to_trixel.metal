#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_lighting_to_trixel.glsl. Screen-space lighting
// application pass — modulates trixelColors.rgb by the per-pixel AO
// factor written to canvasAO.r by COMPUTE_VOXEL_AO.

struct FrameDataLightingToTrixel {
    int lightingEnabled;
    int _padding0;
    int _padding1;
    int _padding2;
};

kernel void c_lighting_to_trixel(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasAO [[texture(2)]],
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

    const float ao = canvasAO.read(uint2(pixel)).r;
    float4 color = trixelColors.read(uint2(pixel));
    color.rgb *= ao;
    trixelColors.write(color, uint2(pixel));
}
