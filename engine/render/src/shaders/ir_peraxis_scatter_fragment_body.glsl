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
// Per-fragment planar depth from the hardware-rasterized finite face.
noperspective in float vDepth;
noperspective in vec2 vQuadParam;
// Face-center iso-depth for per-face depth-color; flat — constant across the
// face instance.
flat in float vIsoDepth;
flat in int vDepthColorMode;
flat in float vDepthColorExtent;
// Face/cell priority within a depth band.
flat in float vCellTieOffset;

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
    const float finalDepth = scatterFinalDepth(vDepth, vCellTieOffset);
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
    const vec3 position = perAxisFaceClosestPoint(vFaceOrigin, vFaceId, vQuadParam);
    const float visibility = shadowsEnabled == 0 ? 1.0 :
        worldSurfaceSunShadowFactor(
            position, faceOutwardNormal6(vFaceId), pos3DtoDistance(position),
            sunCasterViewToWorld
        );
    FragColor = vec4(visibility >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0), vColor.a);
#elif IR_PER_AXIS_SURFACE_LIGHTING
    const vec3 position = perAxisFaceClosestPoint(vFaceOrigin, vFaceId, vQuadParam);
    // Overflow faces have no AO owner texel; their material uses AO 1.
    const float ao = vOwnerPixel.x < 0 ? 1.0 : texelFetch(surfaceAO, vOwnerPixel, 0).r;
    FragColor = vec4(worldSurfaceLighting(vColor.rgb, ao, position,
                      faceOutwardNormal6(vFaceId), sunCasterViewToWorld, vFaceOrigin), vColor.a);
#else
    if (vDepthColorMode == -1) {
        FragColor = vec4(vColor.rgb * 0.4, 1.0);
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
