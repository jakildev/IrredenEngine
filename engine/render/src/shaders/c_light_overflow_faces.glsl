#version 450 core

// View-visibility overflow-face lighting (#2334, epic #2331 phase C2).
//
// C1 (#2333) appends the view-visible faces the cardinal-keyed per-axis store
// drops — the set `viewVisible \ cardinalWinners` — into a bounded overflow
// list and the framebuffer scatter draws them ALBEDO-only (unlit slivers beat
// missing geometry). This compute pass, dispatched inside LIGHTING_TO_TRIXEL
// AFTER the per-axis CELL lighting (so the baked sun-shadow map at slot 28 and
// the 128^3 light volume are already bound), relights each overflow entry at its
// recovered WORLD position — sun-shadow cascade + light-volume + Lambert,
// AO = 1.0 — and rewrites the entry's stored colorPacked in place. The unchanged
// scatter then composites LIT slivers while rotating.
//
// Accepted drift vs a real per-axis cell: no screen-space AO (overflow faces own
// no canvas cell; the epic's accepted-drift note covers it). The world sample
// path mirrors c_lighting_to_trixel.glsl's per-axis + P4b-2 world-receive
// branches (same sun/volume/Lambert/HDR math + shared helpers), so a revealed
// sliver shades consistently with the adjacent lit cells.
//
// Runs ONLY while rotating (per-axis canvases allocated). The cardinal fast path
// never dispatches this kernel, so yaw-0 output is byte-identical.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"        // decode*, faceOutwardNormal6, unpack/packColor
#include "ir_per_axis_lighting.glsl" // perAxisCellToWorld3D
// ir_sun_projection.glsl must precede ir_sun_shadow_sample.glsl (the include
// resolver is non-recursive) — same order as c_lighting_to_trixel.glsl.
#include "ir_sun_projection.glsl"
#include "ir_sun_shadow_sample.glsl" // FrameDataSun(29), sun-depth SSBO(28), worldSunShadowFactor()

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    int   lightVolumeEnabled;
    float debugLightLevel;
    int   debugOverlayMode;
    int   hdrEnabled;
    float exposure;
    float skyIntensity;
    vec4  skyColor;
};

// Prefix of FrameDataVoxelToCanvas (binding 7) through overflowScratchLayout_
// (offset 208). Only the fields this pass reads are named; every other field is
// padded so the std140 offsets stay in lockstep with the C++ struct and the
// c_lighting_to_trixel.glsl declaration this mirrors.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    vec2  frameCanvasOffset;
    ivec2 _trixelCanvasOffsetZ1;
    ivec2 voxelRenderOptions;
    ivec2 _voxelDispatchGrid;
    int   _voxelCount;
    int   _perAxisRoute;
    ivec2 canvasSizePixels;
    ivec2 _cullIsoMin;
    ivec2 _cullIsoMax;
    float _visualYaw;
    float _rasterYaw;
    float _residualYaw;
    float _isDetachedCanvas;
    vec4  _faceDeformPadding[3];
    ivec4 visibleFaceIds;
    vec4  _voxelDepthAxisUnused;
    vec4  _detachedWorldReceive;
    ivec4 _visibleIsoBounds;
    int   _resolveMode;
    int   _occlusionCullMipCount;
    int   _feederSubCap;
    int   _feederPassTailBase;
    ivec4 overflowScratchLayout;   // .x view-mask base, .y ctrl base, .z entry base, .w cap
};

layout(std140, binding = 23) uniform LightVolumeParams {
    int   _gridSize;
    int   _halfExtent;
    int   _lightCount;
    float _stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

layout(binding = 3) uniform sampler2D paletteLUT;
layout(binding = 5) uniform sampler3D lightVolume;
// Winning-light ID volume (#2318), image unit 7 — read (NEAREST) only on the
// has-SPOT path to attenuate a spot winner's volume contribution.
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;

// The overflow entry list + ctrl block live in the per-axis resolve scratch.
// Slot 28 is held by the sun-depth map this pass samples, so the scratch is
// bound here at kBufferIndex_OverflowLightingScratch (a buffer slot dead during
// LIGHTING_TO_TRIXEL). Whole-buffer bind; region offsets come from
// overflowScratchLayout above (all in uints). Entry i: 3 uints at
// overflowScratchLayout.z + i*3 = {packedCardCell, colorPacked, encodedDist};
// live entry count = scratch[overflowScratchLayout.y + 1] (ctrl instanceCount).
layout(std430, binding = 8) buffer OverflowLightingScratch {
    uint overflowScratch[];
};

// --- shared world-lighting primitives -----------------------------------------
// These MIRROR c_lighting_to_trixel.glsl (kept in lockstep). Duplicated rather
// than extracted so the shipped lighting kernel stays byte-identical — a future
// simplify pass can hoist both into a shared ir_world_lighting.glsl include.
struct GPULightSource {
    vec4 originAndType;
    vec4 colorAndIntensity;
    vec4 directionAndRadius;
    vec4 coneAndSeedAlpha;
    vec4 trueOriginVoxel;
};
layout(std430, binding = 4) readonly buffer LightSourceBuffer {
    GPULightSource lights[];
};

const float kLightVolumeSize = 128.0;
const float kLightVolumeHalfExtent = 64.0;
const int   kLightTypeSpot = 3;
const float kConeEdgeSoftness = 1.15;

float spotConeFactor(int lightIdx, vec3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const vec3 axis = normalize(L.directionAndRadius.xyz);
    const vec3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4) {
        return 1.0;
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = radians(L.coneAndSeedAlpha.x * 0.5);
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, radians(90.0)));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
// -----------------------------------------------------------------------------

void main() {
    // The dispatch is a 2-D group grid (voxelDispatchGridForCount wraps past
    // 1024 groups in X), so flatten the group coordinates to the entry index.
    const uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    const uint gid = workGroupIndex * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
    // Live entry count (ctrl block instanceCount, ctrlBase + 1). Threads past it
    // early-return — the dispatch grid is sized to the worst-case cap.
    const uint entryCount = overflowScratch[uint(overflowScratchLayout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }
    // UNLIT overlay + false-color debug overlays leave overflow slivers as raw
    // albedo (the lit cells route through those modes separately; the negligible
    // sliver set is not worth a debug-parity branch), so the pass is a no-op there.
    if (lightingEnabled == 0 || debugOverlayMode != 0) {
        return;
    }

    const uint entryBase = uint(overflowScratchLayout.z) + gid * 3u;
    const uint packedCell = overflowScratch[entryBase + 0u];
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int  rawDist = int(overflowScratch[entryBase + 2u]);

    // Empty-slot guard mirrors the scatter's alpha<0.1 degenerate test: never
    // relight (and never repack) a cleared cell.
    const vec4 albedo = unpackColor(colorPacked);
    if (albedo.a < 0.1) {
        return;
    }

    // Recover the face's cardinal store cell, its world FaceId, and its world
    // position — bit-for-bit the same decode the scatter's overflow branch uses
    // (v_peraxis_scatter.glsl) and the same world recovery the per-axis CELL
    // lighting uses. The per-axis store is base-resolution, so rawDepth
    // (decodeDepthPerAxis) is world units. Sub-cell recovery, not
    // lattice-only — the sun/volume samples must land on the drawn surface
    // (see perAxisCellToWorld3DSubCell).
    const ivec2 cell = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int faceId = visibleFaceIds[slot] ^ flip;
    const vec3 worldNormal = faceOutwardNormal6(faceId);
    const vec3 pos3D = perAxisCellToWorld3DSubCell(
        cell, rawDist, faceId, canvasSizePixels, frameCanvasOffset, voxelRenderOptions
    );

    // World-space lighting — mirrors c_lighting_to_trixel's world sample.
    // AO = 1.0 (accepted drift), sun-shadow via the shared cascade lookup at the
    // face's own world pos + normal (an overflow face owns no precomputed
    // canvasSunShadow texel), Lambert on the world normal. The sun-shadow darkens
    // only the directional term so a self-shadowed sliver keeps its ambient floor.
    const float ao = 1.0;
    const float shadow = shadowsEnabled != 0
        ? worldSunShadowFactor(pos3D, worldNormal, float(rawDepth))
        : 1.0;
    const float lambert = max(0.0, dot(worldNormal, sunDirection.xyz));
    const float faceFactor =
        (sunAmbient + (1.0 - sunAmbient) * lambert * shadow) * sunIntensity;

    vec3 baseRgb;
    if (lutEnabled == 0) {
        baseRgb = albedo.rgb * ao * faceFactor;
    } else {
        const float luminance = dot(albedo.rgb, vec3(0.299, 0.587, 0.114));
        const vec4 lut = texture(paletteLUT, vec2(ao, luminance));
        baseRgb = albedo.rgb * lut.rgb * faceFactor;
    }

    // Light-volume bleed at the recovered world pos (+ SPOT cone shaping),
    // identical to the cell path.
    if (lightVolumeEnabled != 0) {
        const vec3 localPos = pos3D - vec3(lightVolumeWorldOrigin.xyz);
        const vec3 sampleCoord =
            (localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)) / vec3(kLightVolumeSize);
        const vec4 lightSample = texture(lightVolume, sampleCoord);
        vec3 light = lightSample.rgb * lightSample.a;
        if (lightVolumeWorldOrigin.w != 0) {
            const ivec3 idCell =
                ivec3(floor(localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)));
            if (all(greaterThanEqual(idCell, ivec3(0))) &&
                all(lessThan(idCell, ivec3(int(kLightVolumeSize))))) {
                const int winId = int(round(imageLoad(lightVolumeId, idCell).r * 255.0));
                if (winId > 0 && int(lights[winId - 1].originAndType.w) == kLightTypeSpot) {
                    light *= spotConeFactor(winId - 1, pos3D);
                }
            }
        }
        baseRgb = baseRgb + albedo.rgb * light;
    }

    if (hdrEnabled != 0) {
        if (skyIntensity > 0.0) {
            const float skyFactor = max(0.0, worldNormal.z);
            baseRgb += skyColor.rgb * skyIntensity * skyFactor * ao;
        }
        baseRgb = ACESFilm(baseRgb * exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0, 1.0);
    }

    overflowScratch[entryBase + 1u] = packColor(vec4(baseRgb, albedo.a));
}
