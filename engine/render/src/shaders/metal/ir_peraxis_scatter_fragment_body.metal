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
    // Analytic edge-aware coverage. The visit-bound
    // dilation only guarantees this fragment was VISITED; the coverage DECISION
    // is here, from the fragment's position in the true [0,1]^2 footprint
    // (in.quadParam) and its per-edge interior/boundary flags. Hard-thresholded for
    // the depth co-sort write (no alpha blend). fwidth() before any non-uniform
    // discard so the derivative is valid (the alpha discard is on a flat varying —
    // uniform across the instance).
    const float coverage = scatterAnalyticEdgeCoverage(
        in.quadParam, fwidth(in.quadParam), in.edgeInterior);
    if (coverage < 0.5f) {
        discard_fragment();
    }
    // Margin-yield: fragments outside the exact [0,1]^2 footprint are
    // conservative-dilation margin and only fill pixels no exact footprint
    // claims — mirror of f_peraxis_scatter.glsl.
    const bool inMargin = any(in.quadParam < float2(0.0)) || any(in.quadParam > float2(1.0));
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope so a
    // cell-deep margin yields the shared ridge to the neighbor face's exact
    // footprint while a sub-pixel gap-fill still wins — mirror of
    // f_peraxis_scatter.glsl.
    const float2 outside = max(max(-in.quadParam, in.quadParam - float2(1.0)), float2(0.0));
    // Interior-edge yield floor: a margin that penetrated an INTERIOR
    // edge is extending over the adjacent visible face — floor its yield
    // slope at the cross-face divergence bound so it always loses to that
    // face's exact fragments. The penetrated side
    // is u/v-low when quadParam < 0, u/v-high when > 1; edgeInterior packs
    // (u-low, u-high, v-low, v-high).
    const float interiorU =
        (in.quadParam.x < 0.5f) ? in.edgeInterior.x : in.edgeInterior.y;
    const float interiorV =
        (in.quadParam.y < 0.5f) ? in.edgeInterior.z : in.edgeInterior.w;
    const float gradU = (interiorU > 0.5f)
        ? max(in.marginYieldGradU, in.marginYieldGradFloor)
        : in.marginYieldGradU;
    const float gradV = (interiorV > 0.5f)
        ? max(in.marginYieldGradV, in.marginYieldGradFloor)
        : in.marginYieldGradV;
    // The flat interior term (marginInteriorYieldBias) covers the
    // penetration-INDEPENDENT (flip<<2)|slot key gap between adjacent faces;
    // the floored slope covers the penetration-proportional plane divergence.
    const bool interiorPen = (outside.x > 0.0f && interiorU > 0.5f) ||
                             (outside.y > 0.0f && interiorV > 0.5f);
    const float yieldBias = in.marginBias + outside.x * gradU + outside.y * gradV +
        (interiorPen ? in.marginInteriorYieldBias : 0.0f);
    // Band-quantize + cell-code injection — mirror of f_peraxis_scatter.glsl
    // (exact power-of-two float ops on both backends).
    const float scatterDepth = in.depth + (inMargin ? yieldBias : 0.0f);
    const float finalDepth =
        scatterFinalDepth(scatterDepth, in.cellTieOffset, inMargin);
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
    // A conservative margin has no point on the finite face to query.
    if (inMargin) {
        out.color = float4(1.0, 1.0, 0.0, in.color.a);
    } else {
        const float3 position = perAxisFaceSurfacePoint(in.faceOrigin, in.faceId, in.quadParam);
        const float visibility = sunFrameData.shadowsEnabled == 0 ? 1.0 :
            worldSurfaceSunShadowFactor(
                position, faceOutwardNormal6(in.faceId), pos3DtoDistance(position),
                sunFrameData.sunCasterViewToWorld, sunFrameData, sunDepthBuf
            );
        out.color = float4(visibility >= 0.999 ? float3(0.0) : float3(1.0, 0.0, 1.0), in.color.a);
    }
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
        // Margin-classification overlay — mirror of
        // f_peraxis_scatter.glsl: bright = margin fragment, dim = exact.
        out.color = float4(in.color.rgb * (inMargin ? 1.0f : 0.4f), 1.0f);
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
