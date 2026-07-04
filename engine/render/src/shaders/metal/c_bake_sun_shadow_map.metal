#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
#include <metal_atomic>

// Mirrors shaders/c_bake_sun_shadow_map.glsl. Projects each rasterized
// iso pixel into both cascade regions of the sun shadow depth buffer.

constant int kEmptyDistanceEncoded = 65535;
constant int kSunShadowMapDim = 1024;
constant int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
constant float kSunDepthScale = 1024.0;
constant float kSunDepthOffset = 512.0;

// Cap on the per-source-pixel footprint splat radius (#1784), in sun-map
// texels per side. A backstop only: the splat radius is derived from the
// screen pixel's true sun-space footprint (see the kernel body), so this bounds
// pathological cases where the sun map is far finer than the screen and keeps
// the shadow from bloating past the silhouette by more than a few texels.
constant int kSunBakeSplatMaxRadius = 3;

inline uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
}

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

// Splat `sunZ` across the sun-map texels this source pixel's footprint covers.
// `footprintUV` is the HALF-extent (sun-UV world units) of one screen pixel's
// projection onto the sun plane; dividing by this cascade's texel size gives the
// per-side radius in texels. footprintUV == 0 (every non-cardinal caller) yields
// radius 0 -> a single-texel atomic_min at the floored center, byte-identical to
// the pre-#1784 one-texel scatter. Mirrors shaders/c_bake_sun_shadow_map.glsl.
inline void bakeCascade(
    float2 sunUV, float sunZ, float2 footprintUV, float2 origin, float2 texelSz,
    int cascadeOffset, device atomic_uint *sunDepthBuf
) {
    int2 center = int2(floor((sunUV - origin) / texelSz));
    int2 radius = clamp(int2(ceil(footprintUV / texelSz)), int2(0), int2(kSunBakeSplatMaxRadius));
    uint packedDepth = packSunDepth(sunZ);
    for (int dy = -radius.y; dy <= radius.y; ++dy) {
        int py = center.y + dy;
        if (py < 0 || py >= kSunShadowMapDim) continue;
        for (int dx = -radius.x; dx <= radius.x; ++dx) {
            int px = center.x + dx;
            if (px < 0 || px >= kSunShadowMapDim) continue;
            atomic_fetch_min_explicit(
                &sunDepthBuf[cascadeOffset + py * kSunShadowMapDim + px],
                packedDepth,
                memory_order_relaxed
            );
        }
    }
}

kernel void c_bake_sun_shadow_map(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device atomic_uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        return;
    }
    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (frameData.perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. Per-axis stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    float3 pos3D;
    if (frameData.perAxisRoute != 0) {
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, frameData.visibleFaceIds[encoded & 3], size,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions
        );
    } else if (frameData.residualYaw != 0.0) {
        // Smooth-yaw cast (#1719). While rotating, the single canvas's
        // remaining SDF/text content is stored at the FULL visualYaw with
        // view-frame depth (#1345/#1370) — recover with the matching smooth
        // inverse so those casters bake at their true world positions. The
        // CARDINAL-layout resolve textures (per-axis #1435 + world-placed
        // P4b-3) bake with residualYaw zeroed by the C++ driver, so they keep
        // the cardinal recovery below. Mirrors GLSL.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
    }

    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 uHat = sunFrameData.sunBasisU.xyz;
    float3 vHat = sunFrameData.sunBasisV.xyz;
    float2 sunUV = float2(dot(pos3D, uHat), dot(pos3D, vHat));
    float sunZ = -dot(pos3D, sunDir);

    // Cardinal single-canvas footprint splat (#1784). At cardinal camera yaw the
    // GRID spin cubes route VOXEL_TO_TRIXEL -> here with no resolve stage between,
    // so this is the one caster path still scattering a single sun texel per
    // source pixel. The screen->sun projection lands consecutive source pixels
    // more than one texel apart, so the floor's 2x2 PCF gather (ir_sun_shadow_
    // sample.glsl) falls between them and reads no occluder -> ~87% lit holes /
    // dithered speckle. Recover the sun-space footprint of one screen pixel (the
    // reconstruction is affine in `pixel` at constant depth, so this is a frame-
    // constant tangent-plane footprint, independent of surface angle) and splat
    // across it so the gather always lands on a written occluder inside the
    // silhouette. The per-axis / smooth-yaw / world-placed inputs were already
    // footprint-densified upstream (#1734/#1596), so their footprint stays zero
    // -> single-texel, keeping continuous-yaw + resolved output byte-identical.
    // Mirrors shaders/c_bake_sun_shadow_map.glsl.
    float2 footprintUV = float2(0.0);
    if (frameData.perAxisRoute == 0 && frameData.residualYaw == 0.0) {
        float3 posDx = trixelCanvasPixelToWorld3D(
            pixel + int2(1, 0), rawDepth, frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions, frameData.rasterYaw
        );
        float3 posDy = trixelCanvasPixelToWorld3D(
            pixel + int2(0, 1), rawDepth, frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions, frameData.rasterYaw
        );
        float2 duvX = float2(dot(posDx - pos3D, uHat), dot(posDx - pos3D, vHat));
        float2 duvY = float2(dot(posDy - pos3D, uHat), dot(posDy - pos3D, vHat));
        // Half-extent covers the source pixel's OWN cell in each sun axis. The
        // written distances live on the even-parity iso sublattice (VOXEL_TO_
        // TRIXEL writes only (isoRel.x+isoRel.y) even), so a written pixel owns
        // the diamond out to its diagonal neighbours ~1 screen pixel away on each
        // axis; |duvX|+|duvY| is that ±1px box projected to the sun plane (no 0.5
        // halving — the sublattice spacing is 2px, so half of it is one full px).
        footprintUV = abs(duvX) + abs(duvY);
    }

    bakeCascade(sunUV, sunZ, footprintUV, sunFrameData.cascadeOriginUV_0,
                sunFrameData.cascadeTexelSize_0, 0, sunDepthBuf);
    bakeCascade(sunUV, sunZ, footprintUV, sunFrameData.cascadeOriginUV_1,
                sunFrameData.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf);
}
