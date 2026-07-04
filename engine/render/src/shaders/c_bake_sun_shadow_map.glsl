#version 450 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"

const int kEmptyDistanceEncoded = 65535;
const int kSunShadowMapDim = 1024;
const int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;

const float kSunDepthScale = 1024.0;
const float kSunDepthOffset = 512.0;

// Cap on the per-source-pixel footprint splat radius (#1784), in sun-map
// texels per side. A backstop only: the splat radius is derived from the
// screen pixel's true sun-space footprint (see main()), so this bounds
// pathological cases where the sun map is far finer than the screen and
// keeps the shadow from bloating past the silhouette by more than a few texels.
const int kSunBakeSplatMaxRadius = 3;

uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
}

layout(std430, binding = 28) restrict buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = baking
    // a per-axis voxel canvas into the shared sun map (#1311).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path (#1311).
    uniform ivec4 visibleFaceIds;
};

layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float _cascadePad0;
    uniform float _cascadePad1;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;

// Splat `sunZ` across the sun-map texels this source pixel's footprint covers.
// `footprintUV` is the HALF-extent (sun-UV world units) of one screen pixel's
// projection onto the sun plane; dividing by this cascade's texel size gives the
// per-side radius in texels. footprintUV == 0 (every non-cardinal caller) yields
// radius 0 -> a single-texel atomicMin at the floored center, byte-identical to
// the pre-#1784 one-texel scatter.
void bakeCascade(vec2 sunUV, float sunZ, vec2 footprintUV, vec2 origin, vec2 texelSz, int cascadeOffset) {
    ivec2 center = ivec2(floor((sunUV - origin) / texelSz));
    ivec2 radius = clamp(ivec2(ceil(footprintUV / texelSz)), ivec2(0), ivec2(kSunBakeSplatMaxRadius));
    uint packedDepth = packSunDepth(sunZ);
    for (int dy = -radius.y; dy <= radius.y; ++dy) {
        int py = center.y + dy;
        if (py < 0 || py >= kSunShadowMapDim) continue;
        for (int dx = -radius.x; dx <= radius.x; ++dx) {
            int px = center.x + dx;
            if (px < 0 || px >= kSunShadowMapDim) continue;
            atomicMin(sunDepthBuf[cascadeOffset + py * kSunShadowMapDim + px], packedDepth);
        }
    }
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = imageLoad(trixelDistances, pixel).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        return;
    }
    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. A per-axis canvas stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    vec3 pos3D;
    if (perAxisRoute != 0) {
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, visibleFaceIds[encoded & 3], size,
            frameCanvasOffset, voxelRenderOptions
        );
    } else if (residualYaw != 0.0) {
        // Smooth-yaw cast (#1719). While rotating, the single canvas's
        // remaining SDF/text content is stored at the FULL visualYaw with
        // view-frame depth (#1345/#1370) — recover with the matching smooth
        // inverse so those casters bake at their true world positions. The
        // CARDINAL-layout resolve textures (per-axis #1435 + world-placed
        // P4b-3) bake with residualYaw zeroed by the C++ driver, so they keep
        // the cardinal recovery below.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
    }

    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    vec2 sunUV = vec2(dot(pos3D, uHat), dot(pos3D, vHat));
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
    vec2 footprintUV = vec2(0.0);
    if (perAxisRoute == 0 && residualYaw == 0.0) {
        vec3 posDx = trixelCanvasPixelToWorld3D(
            pixel + ivec2(1, 0), rawDepth, trixelCanvasOffsetZ1,
            frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        vec3 posDy = trixelCanvasPixelToWorld3D(
            pixel + ivec2(0, 1), rawDepth, trixelCanvasOffsetZ1,
            frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        vec2 duvX = vec2(dot(posDx - pos3D, uHat), dot(posDx - pos3D, vHat));
        vec2 duvY = vec2(dot(posDy - pos3D, uHat), dot(posDy - pos3D, vHat));
        // Half-extent covers the source pixel's OWN cell in each sun axis. The
        // written distances live on the even-parity iso sublattice (VOXEL_TO_
        // TRIXEL writes only (isoRel.x+isoRel.y) even), so a written pixel owns
        // the diamond out to its diagonal neighbours ~1 screen pixel away on each
        // axis; |duvX|+|duvY| is that ±1px box projected to the sun plane (no 0.5
        // halving — the sublattice spacing is 2px, so half of it is one full px).
        footprintUV = abs(duvX) + abs(duvY);
    }

    bakeCascade(sunUV, sunZ, footprintUV, cascadeOriginUV_0, cascadeTexelSize_0, 0);
    bakeCascade(sunUV, sunZ, footprintUV, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount);
}
