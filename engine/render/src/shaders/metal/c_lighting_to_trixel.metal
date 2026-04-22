#include "ir_iso_common.metal"

// Mirrors shaders/c_lighting_to_trixel.glsl. Screen-space lighting
// application pass — modulates trixelColors.rgb by (AO × sun-shadow),
// with an optional LUT palette shading path keyed off lutEnabled and
// an optional flood-fill light-volume additive contribution keyed off
// lightVolumeEnabled. When the volume path is active, the per-pixel
// world voxel position is recovered from the distance texture and the
// bound 3D light volume is sampled and additively combined with the
// AO base.

struct FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    int   lightVolumeEnabled;
    float debugLightLevel;
    // Mirrors IRRender::DebugOverlayMode. 0 = NONE (artistic path); 1 = AO,
    // 2 = LIGHT_LEVEL, 3 = SHADOW all short-circuit and write false-color.
    int   debugOverlayMode;
};

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
constant float kLightVolumeSize = 128.0;
constant float kLightVolumeHalfExtent = 64.0;

kernel void c_lighting_to_trixel(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    constant FrameDataVoxelToTrixel& voxelFrameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasAO [[texture(2)]],
    texture2d<float, access::sample> paletteLUT [[texture(3)]],
    // canvasSunShadow sits at texture unit 4 — Metal flattens texture
    // and image tables into a shared slot space, so it cannot collide
    // with paletteLUT at unit 3 or lightVolume at unit 5.
    texture2d<float, access::read> canvasSunShadow [[texture(4)]],
    texture3d<float, access::sample> lightVolume [[texture(5)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    if (frameData.lightingEnabled == 0) {
        return;
    }

    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= 65535) {
        return;
    }

    const float  ao     = canvasAO.read(uint2(pixel)).r;
    const float  shadow = canvasSunShadow.read(uint2(pixel)).r;
    const float4 src    = trixelColors.read(uint2(pixel));

    // Debug overlay short-circuits artistic shading and paints a false-
    // color representation of the selected lighting buffer.
    if (frameData.debugOverlayMode != 0) {
        float3 debugColor = float3(0.0f);
        if (frameData.debugOverlayMode == 1) {
            debugColor = float3(1.0f - ao, ao, 0.0f);
        } else if (frameData.debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = float3(level, level, 1.0f);
        } else {
            debugColor = shadow >= 0.999f ? float3(0.0f) : float3(1.0f, 0.0f, 1.0f);
        }
        trixelColors.write(float4(debugColor, src.a), uint2(pixel));
        return;
    }

    float3 baseRgb;
    if (frameData.lutEnabled == 0) {
        baseRgb = src.rgb * ao * shadow;
    } else {
        // LUT palette shading: AO drives the X axis (light level), luminance
        // drives Y. Shadow darkening is applied after the LUT lookup so
        // palette shading and directional shadows compose without needing a
        // 3D LUT.
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        const float  luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
        const float4 lut       = paletteLUT.sample(s, float2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * shadow;
    }

    if (frameData.lightVolumeEnabled != 0) {
        // Recover the world voxel position of this pixel from the encoded
        // depth + iso offset, mirroring the math in c_compute_voxel_ao.metal.
        const int rawDepth = encoded >> 2;
        const int2 isoRel =
            pixel - voxelFrameData.trixelCanvasOffsetZ1 -
            int2(floor(voxelFrameData.frameCanvasOffset));
        const int subdivisions = max(voxelFrameData.voxelRenderOptions.y, 1);
        float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
        if (voxelFrameData.voxelRenderOptions.x != 0) {
            pos3D /= float(subdivisions);
        }

        // Sample the light volume at the surface voxel. CLAMP_TO_EDGE means
        // out-of-volume samples read zero light (border texels were cleared
        // during BFS staging).
        constexpr sampler volumeSampler(
            filter::nearest, address::clamp_to_edge
        );
        const float3 sampleCoord =
            (pos3D + float3(kLightVolumeHalfExtent) + float3(0.5)) /
            float3(kLightVolumeSize);
        const float3 light = lightVolume.sample(volumeSampler, sampleCoord).rgb;
        baseRgb = baseRgb + src.rgb * light;
    }

    trixelColors.write(float4(baseRgb, src.a), uint2(pixel));
}
