#version 460 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO, directional sun
// shadow, flood-fill light volume) and modulates the canvas color in
// place.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    // Main-canvas pass sets this to 1 once the AO texture is bound. GUI
    // canvases always see 0 so GUI-sourced pixels are not modulated.
    uniform int   lightingEnabled;
    // 1 activates LUT palette shading, which replaces the plain grayscale
    // AO multiplication with a luminance-indexed palette lookup driven by
    // the per-pixel AO value.
    uniform int   lutEnabled;
    // 1 activates flood-fill light-volume sampling: per-pixel world
    // voxel position recovered from the distance texture, sampled in the
    // bound 3D light volume, and additively combined with the AO base.
    uniform int   lightVolumeEnabled;
    uniform float debugLightLevel;
    // Mirrors IRRender::DebugOverlayMode. 0 = NONE (artistic path); 1 = AO,
    // 2 = LIGHT_LEVEL, 3 = SHADOW all short-circuit and write false-color.
    uniform int   debugOverlayMode;
    // Additive sky term — `.rgb` is the sky's emissive colour and `.w` is
    // its intensity. The contribution is `skyColor.rgb * skyColor.w * ao`,
    // so AO doubles as a sky-visibility proxy (open sky pixels get the
    // full term; occluded creases see none). `.w == 0.0` disables the
    // term, which is the default so existing LDR demos render unchanged.
    uniform vec4  skyColor;
};

// Mirrors the FrameDataVoxelToTrixel UBO used by VOXEL_TO_TRIXEL stages
// and COMPUTE_VOXEL_AO. We only need a subset of fields here (canvas
// offset + subdivision toggles) but the full layout must match for
// std140 binding compatibility.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
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
};

// trixelColors is RGBA16F to preserve HDR contributions from the light
// volume and the additive sky term; a downstream TONEMAP_TO_TRIXEL pass
// collapses it back to LDR for TRIXEL_TO_FRAMEBUFFER. Keep the AO and
// sun-shadow canvases as RGBA8 — those are scalar modulators, not
// emissives, and don't benefit from extra precision.
layout(rgba16f, binding = 0) uniform image2D trixelColors;
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

// `faceOutwardNormal()` lives in ir_iso_common.glsl — shared with
// c_compute_voxel_ao.glsl so the AO sampling direction and the lambert
// dot product use the same convention.

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
    const int face = encoded & 3;
    const float lambert = max(0.0, dot(faceOutwardNormal(face), sunDirection.xyz));
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
        vec3 pos3D = trixelCanvasPixelToWorld3D(
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
        // No clamp: trixelColors is RGBA16F now and downstream
        // TONEMAP_TO_TRIXEL collapses HDR back into LDR. A clamp here
        // would crush emissive contributions before they reach tonemap.
        baseRgb = baseRgb + src.rgb * light;
    }

    if (skyColor.w > 0.0) {
        baseRgb += src.rgb * skyColor.rgb * skyColor.w * ao;
    }

    imageStore(trixelColors, pixel, vec4(baseRgb, src.a));
}
