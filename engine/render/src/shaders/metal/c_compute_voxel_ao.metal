#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"

// Mirrors shaders/c_compute_voxel_ao.glsl. Per-pixel ambient-occlusion
// compute. Samples four face-tangent neighbour pixels in trixelDistances
// and counts each as occluding when its decoded surface position sits in
// front of the receiver's face plane by ~1 voxel along face-outward.

constant int kEmptyDistanceEncoded = 65535;

// Crease-band along the face-outward normal. Canonical edge-occluder voxel
// sits at d = `kAOOccluderHeight`; the band is centred there with a
// ±`kAOBandHalfWidth` voxel tolerance, extended below by
// `kAOSubVoxelTolerance` for SDF sub-voxel surfaces. Must stay in lockstep
// with the matching constants in c_compute_voxel_ao.glsl.
constant float kAOOccluderHeight = 1.0;
constant float kAOBandHalfWidth = 0.5;
constant float kAOSubVoxelTolerance = 0.375;
constant float kAOMinHeight = kAOOccluderHeight - kAOBandHalfWidth - kAOSubVoxelTolerance;
constant float kAOMaxHeight = kAOOccluderHeight + kAOBandHalfWidth;

// Mirrors `FrameDataSun` from ir_render_types.hpp. Only `aoEnabled` is
// consumed here; the layout must match so the shared UBO at binding 29
// can be read by every consumer (BAKE_SUN_SHADOW_MAP owns the upload).
struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int aoEnabled;
    float4 sunBasisU;
    float4 sunBasisV;
    float2 sunBufferOriginUV;
    float2 sunBufferTexelSize;
    float2 cascadeOriginUV_0;
    float2 cascadeTexelSize_0;
    float2 cascadeOriginUV_1;
    float2 cascadeTexelSize_1;
    float cascadeSplitDepth;
    int cascadeCount;
    float _cascadePad0;
    float _cascadePad1;
};

kernel void c_compute_voxel_ao(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasAO [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    const int kEmpty = (frameData.perAxisRoute != 0) ? 0x7FFFFFFF : kEmptyDistanceEncoded;
    if (encoded >= kEmpty) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.aoEnabled == 0) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    // Decode the visible-triplet slot (0/1/2) the rasterizer wrote, then
    // resolve the world FaceId via `visibleFaceIds[slot]` (#1278). Single
    // source of face metadata shared with the raster.
    int slot = encoded & 3;
    int faceId = frameData.visibleFaceIds[slot];
    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (frameData.perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally (perAxisRoute != 0), recovered via faceOriginFromInPlane; the
    // single canvas uses the cardinal-snap reconstruction. Mirrors GLSL.
    bool perAxis = frameData.perAxisRoute != 0;
    float3 pos3D = perAxis
        ? perAxisCellToWorld3D(
              pixel, rawDepth, faceId, size,
              frameData.frameCanvasOffset, frameData.voxelRenderOptions
          )
        : trixelCanvasPixelToWorld3D(
              pixel,
              rawDepth,
              frameData.trixelCanvasOffsetZ1,
              frameData.frameCanvasOffset,
              frameData.voxelRenderOptions,
              cardinalIndex
          );

    // World-frame outward normal + in-plane tangents for the camera-visible
    // face this pixel rendered. Tangents are rotated through R_z(-rasterYaw)
    // before iso projection so the neighbour-sample iso direction matches
    // where the rasterizer wrote the +tangent neighbour at this cardinal
    // (PR #1275 prep). Mirrors GLSL.
    float3 worldOutward = float3(faceOutwardNormal6I(faceId));
    int3 t1;
    int3 t2;
    if (faceId == kFaceZNeg || faceId == kFaceZPos) {
        t1 = int3(1, 0, 0);
        t2 = int3(0, 1, 0);
    } else if (faceId == kFaceXNeg || faceId == kFaceXPos) {
        t1 = int3(0, 1, 0);
        t2 = int3(0, 0, 1);
    } else {
        // Y_NEG or Y_POS
        t1 = int3(1, 0, 0);
        t2 = int3(0, 0, 1);
    }

    int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    int2 deltaT1;
    int2 deltaT2;
    if (perAxis) {
        // Per-axis canvas is BASE-RESOLUTION (#1458): 1 cell = 1 world voxel.
        deltaT1 = int2(1, 0);
        deltaT2 = int2(0, 1);
    } else {
        int3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
        int3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
        deltaT1 = pos3DtoPos2DIso(t1View) * scale;
        deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    }

    int occl = 0;
    for (int dir = 0; dir < 4; ++dir) {
        int2 delta;
        if (dir == 0) delta = deltaT1;
        else if (dir == 1) delta = -deltaT1;
        else if (dir == 2) delta = deltaT2;
        else delta = -deltaT2;
        int2 samplePixel = pixel + delta;
        if (samplePixel.x < 0 || samplePixel.x >= size.x ||
            samplePixel.y < 0 || samplePixel.y >= size.y) continue;

        int neighbourEncoded = trixelDistances.read(uint2(samplePixel)).x;
        if (neighbourEncoded >= kEmpty) continue;

        int neighbourRawDepth = (frameData.perAxisRoute != 0) ? (neighbourEncoded >> 10) : (neighbourEncoded >> 2);
        float3 neighbourPos3D;
        if (perAxis) {
            int neighbourFaceId = frameData.visibleFaceIds[neighbourEncoded & 3];
            neighbourPos3D = perAxisCellToWorld3D(
                samplePixel, neighbourRawDepth, neighbourFaceId, size,
                frameData.frameCanvasOffset, frameData.voxelRenderOptions
            );
        } else {
            neighbourPos3D = trixelCanvasPixelToWorld3D(
                samplePixel,
                neighbourRawDepth,
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions,
                cardinalIndex
            );
        }

        float d = dot(neighbourPos3D - pos3D, worldOutward);
        if (d > kAOMinHeight && d < kAOMaxHeight) occl++;
    }

    // Each occluding edge-neighbour darkens by 10%; all four caps at 60%
    // brightness. Must stay in lockstep with the 0.10 coefficient in
    // c_compute_voxel_ao.glsl.
    float ao = 1.0 - float(occl) * 0.10;
    canvasAO.write(float4(ao, 0.0, 0.0, 0.0), uint2(pixel));
}
