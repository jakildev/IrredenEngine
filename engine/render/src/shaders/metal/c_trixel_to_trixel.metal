#include <metal_stdlib>
using namespace metal;

// Composites a "from" trixel canvas onto a "to" trixel canvas with per-pixel
// depth comparison.  Mirrors shaders/c_trixel_to_trixel.glsl — used to fold
// the static-shape canvas into the dynamic voxel canvas (and similar passes).

struct FrameDataTrixelToTrixel {
    int2 cameraTrixelOffset;
    int2 trixelCanvasOffsetZ1;
    int2 trixelTextureOffsetZ1;
    float2 texturePos2DIso;
};

kernel void c_trixel_to_trixel(
    constant FrameDataTrixelToTrixel& frameData [[buffer(10)]],
    texture2d<float, access::read_write> trixelColorsTo [[texture(0)]],
    texture2d<int, access::read_write> trixelDistancesTo [[texture(1)]],
    texture2d<float, access::read> trixelColorsFrom [[texture(2)]],
    texture2d<int, access::read> trixelDistancesFrom [[texture(3)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 srcPixel = int2(globalId.xy);
    const int2 srcSize = int2(
        int(trixelColorsFrom.get_width()),
        int(trixelColorsFrom.get_height())
    );
    if (srcPixel.x >= srcSize.x || srcPixel.y >= srcSize.y) {
        return;
    }

    const float4 srcColor = trixelColorsFrom.read(uint2(srcPixel));
    if (srcColor.a == 0.0) {
        return;
    }

    const int srcDistance = trixelDistancesFrom.read(uint2(srcPixel)).x;
    if (srcDistance >= 65535) {
        return;
    }

    const int2 srcOrigin = frameData.trixelTextureOffsetZ1;
    const int2 dstOrigin = frameData.trixelCanvasOffsetZ1 + frameData.cameraTrixelOffset;
    const int2 isoOffset = int2(floor(frameData.texturePos2DIso));
    const int2 dstPixel = (srcPixel - srcOrigin) + dstOrigin + isoOffset;

    const int2 dstSize = int2(
        int(trixelColorsTo.get_width()),
        int(trixelColorsTo.get_height())
    );
    if (dstPixel.x < 0 || dstPixel.x >= dstSize.x ||
        dstPixel.y < 0 || dstPixel.y >= dstSize.y) {
        return;
    }

    const int dstDistance = trixelDistancesTo.read(uint2(dstPixel)).x;
    if (srcDistance > dstDistance) {
        return;
    }
    trixelColorsTo.write(srcColor, uint2(dstPixel));
    trixelDistancesTo.write(int4(srcDistance, 0, 0, 0), uint2(dstPixel));
}
