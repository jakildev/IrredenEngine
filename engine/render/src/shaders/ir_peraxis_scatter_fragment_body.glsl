#ifndef IR_PER_AXIS_VISIBILITY
#define IR_PER_AXIS_VISIBILITY 0
#endif
#ifndef IR_PER_AXIS_SURFACE_SHADOW
#define IR_PER_AXIS_SURFACE_SHADOW 0
#endif
#ifndef IR_PER_AXIS_SURFACE_LIGHTING
#define IR_PER_AXIS_SURFACE_LIGHTING 0
#endif

flat in vec4 vColor;
flat in vec3 vFaceOrigin;
flat in int vFaceId;
flat in ivec2 vOwnerPixel;
flat in ivec3 vVisibilityExtent;
// Per-fragment planar depth + margin-yield classification: vDepth is
// the face plane's exact depth at this fragment (linear interpolation of
// per-corner planar keys); fragments outside the exact [0,1]^2 footprint are
// conservative-dilation margin and yield by vMarginDepthBias, so a margin
// only fills pixels no exact footprint claims (sub-pixel sliver
// gaps) and never beats a same-plane owner via draw order.
noperspective in float vDepth;
noperspective in vec2 vQuadParam;
flat in float vMarginDepthBias;
// Per-axis margin-yield slope, in vDepth units per unit of quad-param
// penetration. A large per-axis margin extrapolates the face plane far enough
// that its depth would beat a neighbor face's exact footprint across a shared
// ridge; scaling the yield by penetration * this slope makes the margin yield in
// proportion to its own extrapolation excursion (the doubled top<->side sliver).
flat in float vMarginYieldGradU;
flat in float vMarginYieldGradV;
// Interior-edge yield-slope floor + flat interior yield. A margin that
// penetrates an INTERIOR edge extends over the ADJACENT visible face, so its
// slope is floored at the cross-face divergence bound and it pays a flat bias
// covering the constant (flip << 2) | slot key gap; boundary (silhouette)
// penetrations keep the tighter own-slope yield.
flat in float vMarginYieldGradFloor;
flat in float vMarginInteriorYieldBias;
// Face-center iso-depth for per-face depth-color; flat — constant across the
// face instance.
flat in float vIsoDepth;
flat in int vDepthColorMode;
flat in float vDepthColorExtent;
// Face/cell priority within a depth band; equal codes need coverage arbitration.
flat in float vCellTieOffset;
// Per-edge interior/boundary classification for the analytic coverage —
// .x = u-low, .y = u-high, .z = v-low, .w = v-high; 1 = interior, 0 = silhouette.
flat in vec4 vEdgeInterior;

out vec4 FragColor;

// HSV → RGB. Keep identical to hsvToRgb in c_shapes_to_trixel_body.glsl so
// voxel-scatter depth-color is bit-exact with the SDF twin when mode is on.
vec3 hsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    if (vColor.a < 0.1) {
        discard;
    }
    // Analytic edge-aware coverage. The visit-bound
    // dilation only guarantees this fragment was VISITED; the coverage DECISION
    // is here, from the fragment's
    // position in the true [0,1]^2 footprint (vQuadParam) and its per-edge
    // interior/boundary flags. Hard-thresholded for the depth co-sort write (no
    // alpha blend). fwidth() before any non-uniform discard so the derivative is
    // valid (the alpha discard is on a flat varying — uniform across the instance).
    const float coverage =
        scatterAnalyticEdgeCoverage(vQuadParam, fwidth(vQuadParam), vEdgeInterior);
    if (coverage < 0.5) {
        discard;
    }
    const bool inMargin = any(lessThan(vQuadParam, vec2(0.0))) ||
                          any(greaterThan(vQuadParam, vec2(1.0)));
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope, so
    // a cell-deep margin (whose plane extrapolation gained a real depth advantage)
    // yields the shared ridge to the neighbor face's exact footprint, while a
    // sub-pixel gap-fill yields almost nothing and still wins.
    const vec2 outside = max(max(-vQuadParam, vQuadParam - vec2(1.0)), vec2(0.0));
    // Interior-edge yield floor: a margin that penetrated an INTERIOR edge
    // is extending over the adjacent visible face — floor its yield slope at the
    // cross-face divergence bound so it always loses to that face's exact
    // fragments. The penetrated side is u/v-low when
    // vQuadParam < 0, u/v-high when > 1; vEdgeInterior packs (u-low, u-high,
    // v-low, v-high).
    const float interiorU = (vQuadParam.x < 0.5) ? vEdgeInterior.x : vEdgeInterior.y;
    const float interiorV = (vQuadParam.y < 0.5) ? vEdgeInterior.z : vEdgeInterior.w;
    const float gradU = (interiorU > 0.5)
        ? max(vMarginYieldGradU, vMarginYieldGradFloor)
        : vMarginYieldGradU;
    const float gradV = (interiorV > 0.5)
        ? max(vMarginYieldGradV, vMarginYieldGradFloor)
        : vMarginYieldGradV;
    // The flat interior term (vMarginInteriorYieldBias) covers the
    // penetration-INDEPENDENT (flip<<2)|slot key gap between adjacent faces; the
    // floored slope covers the penetration-proportional plane divergence.
    const bool interiorPen =
        (outside.x > 0.0 && interiorU > 0.5) || (outside.y > 0.0 && interiorV > 0.5);
    const float yieldBias = vMarginDepthBias + outside.x * gradU + outside.y * gradV +
        (interiorPen ? vMarginInteriorYieldBias : 0.0);
    // Final ties prefer exact coverage when face/cell priorities coincide.
    const float scatterDepth = vDepth + (inMargin ? yieldBias : 0.0);
    const float finalDepth =
        scatterFinalDepth(scatterDepth, vCellTieOffset, inMargin);
#if IR_PER_AXIS_VISIBILITY
    const ivec2 pixel = ivec2(gl_FragCoord.xy);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, vVisibilityExtent.xy))) discard;
    const uint index = uint(pixel.y) * uint(vVisibilityExtent.x) + uint(pixel.x);
    const uint pixels = uint(vVisibilityExtent.x) * uint(vVisibilityExtent.y);
    const bool validDepth = finalDepth >= 0.0 && finalDepth <= 1.0;
    if ((vVisibilityExtent.z & 2) != 0) {
        if ((vVisibilityExtent.z & 1) != 0) atomicAdd(visibilityCodes[pixels], 1u);
        if (validDepth) atomicMin(visibilityCodes[index], scatterVisibilityCode(finalDepth));
        discard;
    } else {
        const bool rejected = validDepth && scatterVisibilityReject(scatterVisibilityCode(finalDepth), visibilityCodes[index]);
        if ((vVisibilityExtent.z & 1) != 0) atomicAdd(visibilityCodes[pixels + (rejected ? 2u : 1u)], 1u);
        if (rejected) discard;
    }
#endif
#if IR_PER_AXIS_SURFACE_SHADOW
    // A conservative margin has no point on the finite face to query.
    if (inMargin) {
        FragColor = vec4(1.0, 1.0, 0.0, vColor.a);
    } else {
        const vec3 position = perAxisFaceSurfacePoint(vFaceOrigin, vFaceId, vQuadParam);
        const float visibility = shadowsEnabled == 0 ? 1.0 :
            worldSurfaceSunShadowFactor(
                position, faceOutwardNormal6(vFaceId), pos3DtoDistance(position),
                sunCasterViewToWorld
            );
        FragColor = vec4(visibility >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0), vColor.a);
    }
#elif IR_PER_AXIS_SURFACE_LIGHTING
    const vec3 position = perAxisFaceClosestPoint(vFaceOrigin, vFaceId, vQuadParam);
    // Overflow faces have no AO owner texel; their material uses AO 1.
    const float ao = vOwnerPixel.x < 0 ? 1.0 : texelFetch(surfaceAO, vOwnerPixel, 0).r;
    FragColor = vec4(worldSurfaceLighting(vColor.rgb, ao, position,
                      faceOutwardNormal6(vFaceId), sunCasterViewToWorld, vFaceOrigin), vColor.a);
#else
    if (vDepthColorMode == -1) {
        // Margin-classification overlay: bright = margin fragment
        // (conservative-dilation fill outside the exact footprint), dim =
        // exact-footprint fragment. Axis hue from the vertex stage.
        FragColor = vec4(vColor.rgb * (inMargin ? 1.0 : 0.4), 1.0);
    } else if (vDepthColorMode != 0) {
        float dColor = vDepthColorExtent;
        float denomC = max((4.0 / 3.0) * dColor, 1.0);
        float t = clamp((vIsoDepth + dColor) / denomC, 0.0, 1.0);
        FragColor = vec4(hsvToRgb(vec3(0.66 * t, 1.0, 1.0)), 1.0);
    } else {
        FragColor = vColor;
    }
#endif
    gl_FragDepth = finalDepth;
}
