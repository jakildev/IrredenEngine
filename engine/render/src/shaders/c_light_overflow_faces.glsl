#version 450 core

// View-visibility overflow-face lighting.
//
// The per-axis stage-1 append writes the view-visible faces the cardinal-keyed
// per-axis store drops — the set `viewVisible \ cardinalWinners` — into a
// bounded overflow list, and the framebuffer scatter draws each entry with its
// stored colorPacked. This compute pass, dispatched inside LIGHTING_TO_TRIXEL
// AFTER the per-axis CELL lighting (so the baked sun-shadow map at slot 28 and
// the 128^3 light volume are already bound), relights each overflow entry at its
// recovered WORLD position — sun-shadow cascade + light-volume + Lambert,
// AO = 1.0 — and rewrites the entry's stored colorPacked in place, so the
// scatter composites LIT slivers while rotating.
//
// Overflow faces own no canvas cell, so they get no screen-space AO. The world
// sample path mirrors c_lighting_to_trixel.glsl's per-axis + world-receive
// branches (same sun/volume/Lambert/HDR math + shared helpers), so a revealed
// sliver shades consistently with the adjacent lit cells.
//
// Runs ONLY while rotating (per-axis canvases allocated); the cardinal path
// never dispatches this kernel.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"        // decode*, faceOutwardNormal6, unpack/packColor
#include "ir_per_axis_lighting.glsl" // perAxisCellToWorld3DSubCell
#include "ir_sun_projection.glsl"    // shared caster/receiver sun-space projection
#include "ir_sun_shadow_sample.glsl" // FrameDataSun(29), sun-depth SSBO(28), worldSunShadowFactor()
#include "ir_world_lighting.glsl"    // GPULightSource list (slot 4), spotConeFactor, ACESFilm
#include "ir_surface_light_volume.glsl"

#include "ir_lighting_frame_data.glsl"

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
// Winning-light ID volume, image unit 7 — read (NEAREST) only on the
// has-SPOT path to attenuate a spot winner's volume contribution.
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;

// The overflow entry list + ctrl block live in the per-axis resolve scratch.
// Slot 28 is held by the sun-depth map this pass samples, so the scratch is
// bound here at kBufferIndex_OverflowLightingScratch (a buffer slot dead during
// LIGHTING_TO_TRIXEL). Whole-buffer bind; region offsets come from
// overflowScratchLayout (all in uints). Entry i: 3 uints at
// overflowScratchLayout.z + i*3 = {packedCardCell, colorPacked, encodedDist};
// live entry count = scratch[overflowScratchLayout.y + 1] (ctrl instanceCount).
layout(std430, binding = 8) buffer OverflowLightingScratch {
    uint overflowScratch[];
};

void main() {
    // The dispatch is a 2-D group grid (the GPU finalizer wraps past
    // 1024 groups in X), so flatten the group coordinates to the entry index.
    const uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    const uint gid = workGroupIndex * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
    // Live entry count (ctrl block instanceCount, ctrlBase + 1). Threads past it
    // early-return in the padded tail of the 2-D dispatch grid.
    const uint entryCount = overflowScratch[uint(overflowScratchLayout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }
    // Only the normals diagnostic recolors overflow faces; other overlays retain albedo.
    if (lightingEnabled == 0 || (debugOverlayMode != 0 && debugOverlayMode != 8)) {
        return;
    }

    const uint entryBase = uint(overflowScratchLayout.z) + gid * 3u;
    const uint packedCell = overflowScratch[entryBase + 0u];
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int  rawDist = int(overflowScratch[entryBase + 2u]);

    const vec4 albedo = unpackColor(colorPacked);

    // Recover the face's cardinal store cell, its world FaceId, and its world
    // position — bit-for-bit the same decode the scatter's overflow branch uses
    // (v_peraxis_scatter.glsl) and the same world recovery the per-axis CELL
    // lighting uses. The per-axis store is base-resolution, so rawDepth
    // (decodeDepthPerAxis) is world units. Sub-cell recovery, not
    // lattice-only — the sun/volume samples must land on the drawn surface.
    const ivec2 cell = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int faceId = visibleFaceIds[slot] ^ flip;
    const vec3 worldNormal = faceOutwardNormal6(faceId);
    if (debugOverlayMode == 8) {
        overflowScratch[entryBase + 1u] = packColor(vec4(worldNormal * 0.5 + 0.5, albedo.a));
        return;
    }
    const vec3 pos3D = perAxisCellToWorld3DSubCell(
        cell, rawDist, faceId, canvasSizePixels, frameCanvasOffset, voxelRenderOptions
    );

    // World-space lighting — mirrors c_lighting_to_trixel's world sample.
    // AO = 1.0 (no canvas cell), sun-shadow via the shared cascade lookup at the
    // face's own world pos + normal (an overflow face owns no precomputed
    // canvasSunShadow texel), Lambert on the world normal. The sun-shadow darkens
    // only the directional term so a self-shadowed sliver keeps its ambient floor.
    const float ao = 1.0;
    const float shadow = shadowsEnabled != 0
        ? worldSunShadowFactor(pos3D, worldNormal, float(rawDepth))
        : 1.0;
    const float lambert = max(0.0, dot(worldNormal, sunDirection.xyz));
    const float faceFactor =
        surfaceSunFactor(sunAmbient, sunIntensity, lambert, shadow);

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
        const vec3 light = surfaceLightVolume(pos3D, lightVolumeWorldOrigin, lightVolume, lightVolumeId);
        baseRgb = baseRgb + albedo.rgb * light;
    }

    if (hdrEnabled != 0) {
        if (skyIntensity > 0.0) {
            baseRgb += surfaceSkyLight(worldNormal, skyColor.rgb, skyIntensity, ao);
        }
    }
    baseRgb = surfaceDisplayColor(baseRgb, exposure, hdrEnabled != 0);

    overflowScratch[entryBase + 1u] = packColor(vec4(baseRgb, albedo.a));
}
