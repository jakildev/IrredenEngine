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
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5) — see c_voxel_to_trixel_stage_1.glsl + #1278.
    // Lighting maps the decoded depth slot → world FaceId for the
    // six-face outward normal used by Lambert + the HDR sky-term.
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

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    const float ao     = imageLoad(canvasAO, pixel).r;
    // Shadow factor is 1.0 for lit pixels and kShadowDarken (0.45) for
    // pixels whose sun ray hit an occluder in COMPUTE_SUN_SHADOW.
    const float shadow = imageLoad(canvasSunShadow, pixel).r;
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

    const int rawDepth = encoded >> 2;
    // Decode the visible-triplet slot the rasterizer wrote (#1278) and
    // resolve the world FaceId via `visibleFaceIds[slot]`. Sun direction
    // lives in the world frame; the six-face `faceOutwardNormal6` gives
    // the matching world-frame surface normal.
    const int slot = encoded & 3;
    const int faceId = visibleFaceIds[slot];
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    vec3 worldNormal = faceOutwardNormal6(faceId);
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

    if (lightVolumeEnabled != 0) {
        // Recover the world voxel position of this pixel from the encoded
        // depth + iso offset, mirroring the math in c_compute_voxel_ao.glsl.
        // Subdivision-aware canvasOffset matches c_compute_voxel_ao.glsl.
        // At cardinalIndex==0 the path collapses to master so yaw=0 stays
        // byte-identical; non-zero cardinal yaw composes R(-rasterYaw)
        // afterward to recover world coordinates.
        // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
        // face-locally, so recover world-pos via faceOriginFromInPlane; the single
        // canvas uses the cardinal-snap reconstruction. The shared world light
        // volume is then sampled the same way for both (per-axis canvases are only
        // allocated while rotating, so the cardinal fast path is byte-identical).
        vec3 pos3D = perAxisRoute != 0
            ? perAxisCellToWorld3D(pixel, rawDepth, faceId, size, frameCanvasOffset, voxelRenderOptions)
            : trixelCanvasPixelToWorld3D(
                  pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
              );

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
