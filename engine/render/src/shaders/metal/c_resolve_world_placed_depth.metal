#include "ir_iso_common.metal"
#include <metal_atomic>
// The cardinal-layout micro-cell emit shared with
// c_resolve_per_axis_screen_depth.
#include "ir_resolve_cardinal_emit.metal"

// Mirrors shaders/c_resolve_world_placed_depth.glsl. Re-projects one opt-in
// world-placed detached re-voxelize canvas (model-frame R32I distance texture)
// into a screen-space front-most iso-depth scratch buffer laid out exactly
// like the main canvas distance texture, so BAKE_SUN_SHADOW_MAP can cast it
// through its cardinal recovery. Scratch is a buffer (not a texture) because
// MSL has no portable image-atomic syntax.

constant int kEmptyDistanceEncoded = 65535;

struct DetachedShadowFrame {
    float4 worldOriginAndDensity;
    float4 viewToWorld;
};

kernel void c_resolve_world_placed_depth(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    constant DetachedShadowFrame& casterFrame [[buffer(16)]],
    texture2d<int, access::read> detachedDistances [[texture(0)]],
    device atomic_int* resolveScratch [[buffer(28)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 cell = int2(globalId.xy);
    const int2 detachedSize =
        int2(int(detachedDistances.get_width()), int(detachedDistances.get_height()));
    if (cell.x >= detachedSize.x || cell.y >= detachedSize.y) {
        return;
    }

    const int rawDist = detachedDistances.read(uint2(cell)).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty detached cell
    }
    // Single-canvas encoding: the flip is re-emitted into the re-projected
    // encode so polarity survives the resolve bridge.
    const int rawDepth = decodeDepthSingle(rawDist);
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipSingle(rawDist);

    const int2 sourceOptions = int2(frameData.voxelRenderOptions.x,
                                        int(casterFrame.worldOriginAndDensity.w));
    const float3 viewLocalPos = trixelCanvasPixelToWorld3D(
        cell, rawDepth, trixelOriginOffsetZ1(detachedSize), float2(0.0f), sourceOptions, 0
    );
    const float3 worldPoint = rotateByQuat(viewLocalPos, casterFrame.viewToWorld)
                            + casterFrame.worldOriginAndDensity.xyz;
    const int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    const int3 worldPos = roundHalfUp(worldPoint * float(scale));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1.metal's cardinal store, so the BAKE cardinal
    // recovery (trixelCanvasPixelToWorld3D) inverts it exactly.
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    int3 viewPos = worldPos;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation with no lower-corner shift, mirroring the
        // stage-1 cardinal store and the BAKE recovery (trixelCanvasPixelToWorld3D).
        viewPos = rotateCardinalZ(worldPos, cardinalIndex);
    }

    const int2 canvasSize = frameData.canvasSizePixels; // MAIN canvas size
    const int2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSize),
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    const float3 worldNormal = rotateByQuat(faceOutwardNormal6(slot << 1), casterFrame.viewToWorld);
    const float3 viewNormal = abs(rotateCardinalZInv(worldNormal, (4 - cardinalIndex) & 3));
    const int viewAxis = viewNormal.x >= viewNormal.y && viewNormal.x >= viewNormal.z
        ? kXFace : (viewNormal.y >= viewNormal.z ? kYFace : kZFace);
    emitResolveCardinalDiamond(
        resolveScratch, viewPos, viewAxis, slot, flip, mainBase, canvasSize
    );
}
