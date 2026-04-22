#include "ir_iso_common.metal"

// Mirrors shaders/c_fog_to_trixel.glsl. Screen-space fog-of-war pass —
// recovers each rasterized pixel's source-voxel column, samples the 2D
// fog visibility texture at that (x,y) cell, and modulates trixelColors:
//   visible    (.r ~= 1.0) — pass through
//   explored   (.r ~= 0.5) — desaturate + darken
//   unexplored (.r == 0.0) — black

constant int kFogOfWarSize = 256;
constant int kFogOfWarHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
constant float kFogVisibleThreshold = 0.75f;
constant float kFogExploredThreshold = 0.25f;

kernel void c_fog_to_trixel(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }

    const int rawDepth = encoded >> 2;
    const int2 isoRel =
        pixel - frameData.trixelCanvasOffsetZ1 - int2(floor(frameData.frameCanvasOffset));
    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (frameData.voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    const int3 surfaceVoxel = int3(round(pos3D));

    // Iso convention: X-Y is the floor plane, +Z is the downward height
    // axis, so the fog grid lookup is (x, y) with half-extent offset.
    const int2 fogCell = int2(
        surfaceVoxel.x + kFogOfWarHalfExtent,
        surfaceVoxel.y + kFogOfWarHalfExtent
    );
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    if (fogCell.x < 0 || fogCell.x >= fogSize.x ||
        fogCell.y < 0 || fogCell.y >= fogSize.y) {
        // Out-of-range columns are treated as visible. Matches the
        // image-binding wrap-mode invariant documented in the component
        // header — texture2d<...>::read has no sampler wrap-mode
        // behavior, so the bounds check is load-bearing.
        return;
    }

    const float state = canvasFogOfWar.read(uint2(fogCell)).r;
    if (state >= kFogVisibleThreshold) {
        return;
    }

    const float4 src = trixelColors.read(uint2(pixel));
    if (state >= kFogExploredThreshold) {
        const float luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
        trixelColors.write(
            float4(float3(luminance) * 0.4f, src.a),
            uint2(pixel)
        );
        return;
    }

    trixelColors.write(float4(0.0f, 0.0f, 0.0f, src.a), uint2(pixel));
}
