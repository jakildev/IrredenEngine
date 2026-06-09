#version 450 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO, directional sun
// shadow, flood-fill light volume) and modulates the canvas color in
// place.
//
// When hdrEnabled is set, the pass computes in unclamped float precision,
// adds the sky-term contribution, applies exposure, and tonemaps via the
// ACES Filmic curve before writing back to the RGBA8 canvas. The HDR
// dynamic range lives entirely in shader-local variables; no canvas
// format change is needed for v1.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), and worldSunShadowFactor() — for
// the opt-in detached re-voxelize world-receive path (#1576 P4b-2). Shared with
// c_compute_sun_shadow; replaces this pass's former local FrameDataSun block.
#include "ir_sun_shadow_sample.glsl"

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    uniform int   lightingEnabled;
    uniform int   lutEnabled;
    uniform int   lightVolumeEnabled;
    uniform float debugLightLevel;
    uniform int   debugOverlayMode;
    uniform int   hdrEnabled;
    uniform float exposure;
    uniform float skyIntensity;
    uniform vec4  skyColor;
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = lighting
    // a per-axis canvas (#1311), reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // 1.0 for a detached entity canvas (re-voxelize solid), 0.0 for the world
    // canvas. A detached re-voxelize canvas carries no sun-shadow map / light
    // volume (#1558) — the branch below forces shadow = 1.0 and disables the
    // light-volume term so slots 4/5 (inert placeholders) are never sampled.
    uniform float isDetachedCanvas;
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5) — see c_voxel_to_trixel_stage_1.glsl + #1278.
    // Lighting maps the decoded depth slot → world FaceId for the
    // six-face outward normal used by Lambert + the HDR sky-term.
    uniform ivec4 visibleFaceIds;
    uniform vec4 _voxelDepthAxisUnused;   // voxelDepthAxis_ in the full UBO (unused here)
    // World-receive offset (#1576 P4b-2). `.xyz` = the opt-in world-placed
    // detached re-voxelize entity's world cell origin; `.w` = 1.0 when the solid
    // opts into world placement, else 0.0. Recovers each detached voxel's world
    // pos as (model pos + .xyz) for the shared sun-shadow + light-volume sample.
    uniform vec4 detachedWorldReceive;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasAO;
layout(binding = 3) uniform sampler2D paletteLUT;
// canvasSunShadow sits at image unit 4. The Metal backend flattens
// texture_ and imageTexture_ tables into a shared setTexture slot space,
// so it cannot collide with paletteLUT at unit 3 or lightVolume at unit
// 5 — keep the unit numbers in lockstep across GLSL and MSL.
layout(rgba8, binding = 4) readonly uniform image2D canvasSunShadow;
layout(binding = 5) uniform sampler3D lightVolume;

// Phase 1c (#360): the light volume is camera-anchored. The CPU
// uploads `lightVolumeWorldOrigin` (the world voxel that maps to the
// volume's center texel) each frame; subtract it from `pos3D` before
// converting to a sample coordinate. Mirrors LightVolumeParams in
// ir_render_types.hpp — only `.xyz` is meaningful, `.w` is reserved.
// Layout tombstones — must match the propagate/seed UBO layout
// (c_seed_light_volume.glsl, c_propagate_light_volume.glsl). Lighting
// only reads the origin; leading-underscore names mark the unused slots.
layout(std140, binding = 23) uniform LightVolumeParams {
    int _gridSize;
    int _halfExtent;
    int _lightCount;
    float _stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
// The volume covers world voxels in [-half, half) with one texel per
// voxel; sample coords are `(worldVoxel + half + 0.5) / size` to land
// at texel centers.
const float kLightVolumeSize = 128.0;
const float kLightVolumeHalfExtent = 64.0;

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
// Maps [0, ∞) → [0, 1) with a gentle shoulder that preserves color
// saturation in bright highlights better than Reinhard.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    if (lightingEnabled == 0) {
        return;
    }

    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(trixelColors);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    // Empty/background pixels carry kTrixelDistanceMaxDistance (65535).
    // Nothing rasterized here, so nothing to light.
    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= 65535) {
        return;
    }

    // A detached re-voxelize canvas (#1558) is lit by AO + directional sun + sky
    // only by DEFAULT (its slots 4/5 are inert placeholders). The opt-in
    // world-placed path (#1576 P4b-2, detachedWorldReceive.w != 0) instead has it
    // RECEIVE world sun-shadow + 128³ light-volume bleed at its recovered world
    // pos, like an attached GRID solid. The default path stays byte-identical.
    const bool detachedCanvas = isDetachedCanvas != 0.0;
    const bool worldReceive = detachedCanvas && detachedWorldReceive.w != 0.0;

    const int rawDepth = encoded >> 2;
    // Decode the visible-triplet slot (#1278) → world FaceId → world-frame
    // six-face outward normal. Used by Lambert, the HDR sky-term, and the
    // world-receive sun-shadow normal — so hoist it above the shadow read.
    const int slot = encoded & 3;
    const int faceId = visibleFaceIds[slot];
    vec3 worldNormal = faceOutwardNormal6(faceId);

    // Recover this voxel's WORLD position once for an opt-in world-placed
    // detached solid (model pos + the entity's world cell origin); shared by the
    // sun-shadow receive below and the light-volume sample. The detached
    // re-voxelize canvas rasters cardinal (rasterYaw == 0, perAxisRoute == 0), so
    // trixelCanvasPixelToWorld3D recovers its pool-centered MODEL pos.
    vec3 worldReceivePos = vec3(0.0);
    if (worldReceive) {
        worldReceivePos = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        ) + detachedWorldReceive.xyz;
    }

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    const float ao     = imageLoad(canvasAO, pixel).r;
    // Shadow factor: the world canvas reads its per-pixel COMPUTE_SUN_SHADOW
    // result; an opt-in world-placed detached solid re-runs that same cascade
    // lookup at its recovered world pos (receive — world iso depth = model
    // rawDepth + the offset's iso depth picks the cascade); a default detached
    // overlay stays forced fully lit (no shadow map).
    float shadow;
    if (worldReceive) {
        shadow = shadowsEnabled != 0
            ? worldSunShadowFactor(
                  worldReceivePos, worldNormal,
                  float(rawDepth) + detachedWorldReceive.x +
                      detachedWorldReceive.y + detachedWorldReceive.z
              )
            : 1.0;
    } else {
        shadow = detachedCanvas ? 1.0 : imageLoad(canvasSunShadow, pixel).r;
    }
    const vec4  src    = imageLoad(trixelColors, pixel);

    // Debug overlay short-circuits artistic shading and paints a false-
    // color representation of the selected lighting buffer.
    if (debugOverlayMode != 0) {
        vec3 debugColor = vec3(0.0);
        if (debugOverlayMode == 1) {
            debugColor = vec3(1.0 - ao, ao, 0.0);
        } else if (debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = vec3(level, level, 1.0);
        } else {
            debugColor = shadow >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0);
        }
        imageStore(trixelColors, pixel, vec4(debugColor, src.a));
        return;
    }

    // Sun direction lives in the world frame; the six-face `faceOutwardNormal6`
    // (decoded above into worldNormal) gives the matching world-frame normal.
    const float lambert = max(0.0, dot(worldNormal, sunDirection.xyz));
    const float faceFactor = mix(sunAmbient, 1.0, lambert) * sunIntensity;

    vec3 baseRgb;
    if (lutEnabled == 0) {
        baseRgb = src.rgb * ao * shadow * faceFactor;
    } else {
        // LUT palette shading: AO drives the X axis (light level) and pixel
        // luminance selects the palette row so highlights and shadows get
        // distinct cel-shade colour casts. Shadow darkening is applied after
        // the LUT lookup so palette shading and directional shadows compose
        // without needing a 3D LUT.
        const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
        const vec4  lut       = texture(paletteLUT, vec2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * shadow * faceFactor;
    }

    // Light-volume bleed: the world canvas (and per-axis camera canvases) sample
    // the shared 128³ volume; an opt-in world-placed detached solid samples it
    // too, at its recovered world pos (#1576 P4b-2). A default detached overlay
    // stays excluded (placeholder volume never sampled) — byte-identical.
    if (lightVolumeEnabled != 0 && (!detachedCanvas || worldReceive)) {
        // Recover the world voxel position of this pixel from the encoded
        // depth + iso offset, mirroring the math in c_compute_voxel_ao.glsl.
        // Subdivision-aware canvasOffset matches c_compute_voxel_ao.glsl.
        // At rasterYaw==0 the path collapses to master so yaw=0 stays
        // byte-identical; non-zero rasterYaw composes R(-rasterYaw)
        // afterward to recover world coordinates.
        // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
        // face-locally, so recover world-pos via faceOriginFromInPlane; the single
        // canvas uses the cardinal-snap reconstruction. The shared world light
        // volume is then sampled the same way for both (per-axis canvases are only
        // allocated while rotating, so the cardinal fast path is byte-identical).
        // The world-placed detached solid reuses worldReceivePos (model + offset).
        vec3 pos3D = worldReceive
            ? worldReceivePos
            : (perAxisRoute != 0
                ? perAxisCellToWorld3D(pixel, rawDepth, faceId, size, frameCanvasOffset, voxelRenderOptions)
                : trixelCanvasPixelToWorld3D(
                      pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
                  ));

        // Sample the light volume at the surface voxel. CLAMP_TO_EDGE
        // means out-of-volume samples read zero light (the border texels
        // were cleared during volume staging). The propagate pass stores
        // unattenuated emit color in rgb and residual strength in alpha,
        // so the visible contribution is `rgb * alpha` (linear falloff
        // with Manhattan distance, zero past the light's radius).
        // Phase 1c (#360): subtract the camera-anchored world origin so
        // the sample maps to the texel the seed/propagate passes wrote.
        const vec3 localPos =
            pos3D - vec3(lightVolumeWorldOrigin.xyz);
        const vec3 sampleCoord =
            (localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)) /
            vec3(kLightVolumeSize);
        const vec4 lightSample = texture(lightVolume, sampleCoord);
        const vec3 light = lightSample.rgb * lightSample.a;
        baseRgb = baseRgb + src.rgb * light;
    }

    if (hdrEnabled != 0) {
        // Sky-term: upward-facing surfaces receive an additive
        // emissive contribution from the sky hemisphere, gated by
        // AO so recessed surfaces stay dark.
        if (skyIntensity > 0.0) {
            float skyFactor = max(0.0, worldNormal.z);
            baseRgb += skyColor.rgb * skyIntensity * skyFactor * ao;
        }

        // Exposure + ACES Filmic tonemap. The HDR dynamic range lives
        // in the float baseRgb; the tonemap compresses it to [0, 1]
        // before the RGBA8 imageStore.
        baseRgb = ACESFilm(baseRgb * exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0, 1.0);
    }

    imageStore(trixelColors, pixel, vec4(baseRgb, src.a));
}
