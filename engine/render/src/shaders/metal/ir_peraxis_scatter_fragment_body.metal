#ifndef IR_PER_AXIS_VISIBILITY
#define IR_PER_AXIS_VISIBILITY 0
#endif
#ifndef IR_PER_AXIS_SURFACE_SHADOW
#define IR_PER_AXIS_SURFACE_SHADOW 0
#endif
#ifndef IR_PER_AXIS_SURFACE_LIGHTING
#define IR_PER_AXIS_SURFACE_LIGHTING 0
#endif

// HSV → RGB. Keep identical to hsvToRgb in c_shapes_to_trixel_body.metal so
// voxel-scatter depth-color is bit-exact with the SDF twin when mode is on.
static inline float3 hsvToRgb(float3 c) {
    const float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    const float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

fragment FragmentOut IR_PER_AXIS_FRAGMENT_NAME(
    VertexOut in [[stage_in]]
#if IR_PER_AXIS_VISIBILITY
    , device atomic_uint* visibilityCodes [[buffer(26)]]
#endif
#if IR_PER_AXIS_SURFACE_SHADOW || IR_PER_AXIS_SURFACE_LIGHTING
    , constant FrameDataSun& sunFrameData [[buffer(29)]]
    , device const uint* sunDepthBuf [[buffer(28)]]
#endif
#if IR_PER_AXIS_SURFACE_LIGHTING
    , constant FrameDataLightingToTrixel& lighting [[buffer(27)]]
    , constant LightVolumeParams& volumeParams [[buffer(7)]]
    , device const GPULightSource* lights [[buffer(4)]]
    , texture2d<float> paletteLUT [[texture(3)]]
    , texture2d<float> surfaceAO [[texture(4)]]
    , texture3d<float> lightVolume [[texture(5)]]
    , texture3d<float, access::read> lightVolumeId [[texture(7)]]
#endif
) {
    FragmentOut out;
    if (in.color.a < 0.1f) {
        discard_fragment();
    }
    const float finalDepth = scatterFinalDepth(in.depth, in.cellTieOffset);
#if IR_PER_AXIS_VISIBILITY
    const int2 pixel = int2(in.position.xy);
    if (any(pixel < int2(0)) || any(pixel >= in.visibilityExtent.xy)) discard_fragment();
    const uint index = uint(pixel.y) * uint(in.visibilityExtent.x) + uint(pixel.x);
    const uint pixels = uint(in.visibilityExtent.x) * uint(in.visibilityExtent.y);
    const bool validDepth = finalDepth >= 0.0 && finalDepth <= 1.0;
    if ((in.visibilityExtent.z & 2) != 0) {
        if ((in.visibilityExtent.z & 1) != 0) atomic_fetch_add_explicit(&visibilityCodes[pixels], 1u, memory_order_relaxed);
        if (validDepth) atomic_fetch_min_explicit(&visibilityCodes[index], scatterVisibilityCode(finalDepth), memory_order_relaxed);
        discard_fragment();
    } else {
        const bool rejected = validDepth && scatterVisibilityReject(scatterVisibilityCode(finalDepth), atomic_load_explicit(&visibilityCodes[index], memory_order_relaxed));
        if ((in.visibilityExtent.z & 1) != 0) atomic_fetch_add_explicit(&visibilityCodes[pixels + (rejected ? 2u : 1u)], 1u, memory_order_relaxed);
        if (rejected) discard_fragment();
    }
#endif
#if IR_PER_AXIS_SURFACE_SHADOW
    const float3 position = perAxisFaceClosestPoint(in.faceOrigin, in.faceId, in.quadParam);
    const float visibility = sunFrameData.shadowsEnabled == 0 ? 1.0 :
        worldSurfaceSunShadowFactor(
            position, faceOutwardNormal6(in.faceId), pos3DtoDistance(position),
            sunFrameData.sunCasterViewToWorld, sunFrameData, sunDepthBuf
        );
    out.color = float4(visibility >= 0.999 ? float3(0.0) : float3(1.0, 0.0, 1.0), in.color.a);
#elif IR_PER_AXIS_SURFACE_LIGHTING
    const float3 position = perAxisFaceClosestPoint(in.faceOrigin, in.faceId, in.quadParam);
    // Overflow faces have no AO owner texel; their material uses AO 1.
    const float ao = in.ownerPixel.x < 0 ? 1.0 : surfaceAO.read(uint2(in.ownerPixel)).r;
    out.color = float4(worldSurfaceLighting(in.color.rgb, ao, position,
                       faceOutwardNormal6(in.faceId), sunFrameData.sunCasterViewToWorld, in.faceOrigin,
                       lighting, volumeParams, sunFrameData, sunDepthBuf, lights,
                       paletteLUT, lightVolume, lightVolumeId), in.color.a);
#else
    if (in.depthColorMode == -1) {
        out.color = float4(in.color.rgb * 0.4f, 1.0f);
    } else if (in.depthColorMode != 0) {
        float dColor = in.depthColorExtent;
        float denomC = max((4.0f / 3.0f) * dColor, 1.0f);
        float t = clamp((in.isoDepth + dColor) / denomC, 0.0f, 1.0f);
        out.color = float4(hsvToRgb(float3(0.66f * t, 1.0f, 1.0f)), 1.0f);
    } else {
        out.color = in.color;
    }
#endif
    out.depth = finalDepth;
    return out;
}
