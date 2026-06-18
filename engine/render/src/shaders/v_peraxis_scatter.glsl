/*
 * Project: Irreden Engine
 * File: v_peraxis_scatter.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: May 2026
 * -----
 * Smooth camera Z-yaw — T3 (#1310) forward-scatter composite.
 */

#version 450 core

#include "ir_iso_common.glsl"

// Unit quad corner in [-0.5, 0.5]^2 from the shared QuadVAO. (aPos + 0.5)
// gives the {0,1}^2 corner selector for the two in-plane face axes.
layout (location = 0) in vec2 aPos;

layout (binding = 0) uniform sampler2D  triangleColors;
layout (binding = 1) uniform isampler2D triangleDistances;

// binding = 1 is intentionally reused below for the GlobalConstants UBO: in
// GL 4.5 sampler texture-image units and uniform-buffer binding points are
// separate namespaces, so the shared index does not collide (same pattern as
// f_trixel_to_framebuffer.glsl).
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with f_/v_trixel_to_framebuffer (binding 3). The cardinal fast path
// reads only the prefix; the T3 scatter adds perAxisBase / visualYaw /
// visibleFaceIds at the end (std140 append — existing offsets unchanged).
layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    ivec2 perAxisBase;       // canvas-pixel origin of this axis canvas (#1310)
    float visualYaw;         // continuous camera Z-yaw (radians)
    int scatterDebugMode;    // raw DebugOverlayMode; 4/5 = composite instrumentation (#1457)
    ivec4 visibleFaceIds;    // per-slot world FaceId (0..5); .w pad
    // P3b detached fields (unused on the camera path) — declared only to reach
    // scatterFbResolution at the shared std140 offset 176 (#1494).
    vec4 _detachedResidualPad;
    vec4 _detachedDepthAxisPad;
    vec4 scatterFbResolution; // framebuffer .xy for the conservative dilation (#1494)
    // Per-pixel depth-color debug mode (#1697). When depthColorMode != 0 the
    // fragment shader evaluates hue from the interpolated vIsoDepth instead of
    // vColor. depthColorExtent is the bounding half-sum used to normalize [0,1].
    // std140-appended at offset 192; only the scatter shaders read it.
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
};

flat out vec4 vColor;
// Per-fragment PLANAR composite depth (#1457): linear (no-perspective, w==1)
// interpolation of the exact yawed plane depth sampled at each (dilated)
// corner reproduces the face plane's affine depth field at every fragment —
// including the conservative-dilation margin, which extrapolates the same
// plane. Two cells of the SAME face plane then carry identical depth per
// pixel, so the margin-yield bias below (not draw order) decides their
// overlap. A flat per-quad key cannot do this: adjacent same-plane cells get
// different flat keys, and the nearer cell's dilation margin then beats the
// true owner's interior along every cell boundary on the sign-flip side of a
// bracket — the #1457 wrong-voxel-color bands.
noperspective out float vDepth;
// Quad-parameter coords of this corner in the face's in-plane basis: the
// EXACT footprint spans [0,1]^2; dilated corners land outside it. The
// fragment shader classifies margin fragments by this and adds
// vMarginDepthBias so a dilation margin only fills pixels no exact footprint
// claims (the #1494 sub-pixel sliver gaps), never beats a same-plane owner.
noperspective out vec2 vQuadParam;
flat out float vMarginDepthBias;
// Face-center iso-depth for per-face depth-color (#1697). Flat (constant across
// the quad) — origin is the same for all 4 corners of a face instance, so
// interpolation would be a no-op anyway and flat avoids shader-pipeline
// divergence from adding a smooth varying.
flat out float vIsoDepth;
flat out int vDepthColorMode;
flat out float vDepthColorExtent;

// Composite-instrumentation overlay modes (#1457) — raw DebugOverlayMode
// values (ir_render_enums.hpp). Both modes recolor the scattered quad and
// leave vDepth untouched, so the per-pixel depth-test winner is exactly the
// real composite's winner.
const int kOverlayPerAxisId = 4;     // winner identity: X=red, Y=green, Z=blue
const int kOverlayPerAxisOrigin = 5; // recovered-origin field: hue wheel of rawDepth

// Long-period hue wheel for the recovered-origin overlay. rawDepth steps by
// the subdivision density per voxel, so a short or power-of-two period would
// alias against the lattice; 96 gives ~12 voxels per revolution at density 8 —
// adjacent voxels are clearly distinct hues while a clean face reads as a
// smooth progression and a wrong-cell winner as a hue discontinuity.
const float kOriginHuePeriod = 96.0;
vec3 hueWheel(float t) {
    t = fract(t);
    return clamp(
        vec3(abs(t * 6.0 - 3.0) - 1.0, 2.0 - abs(t * 6.0 - 2.0), 2.0 - abs(t * 6.0 - 4.0)),
        0.0,
        1.0
    );
}

// In-plane corner of a face whose `origin` ALREADY sits at the face plane on
// the fixed axis. The store (c_voxel_to_trixel_stage_{1,2}) bakes the polarity
// via faceMicroPositionFixed6 — POS faces store the high-side plane, NEG faces
// the low-side plane — so the recovered depth lands on the face plane and the
// scatter only spans the face's two in-plane world axes (X->y,z  Y->x,z
// Z->x,y). Re-adding the polarity offset here (the
// old per-faceId +1) double-shifts POS faces one cell past the plane: the
// #1310 back-face seam (a ~1px dark gap between the POS face and its neighbors
// at cardinals 1/2/3). cornerSel in {0,1}^2.
vec3 faceSpanCorner(int axis, vec3 origin, vec2 cornerSel) {
    if (axis == 0) return origin + vec3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + vec3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + vec3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

void main() {
    const ivec2 canvasSize = textureSize(triangleDistances, 0);
    const int cell = gl_InstanceID;
    const ivec2 ij = ivec2(cell % canvasSize.x, cell / canvasSize.x);

    const vec4 color = texelFetch(triangleColors, ij, 0);
    // Empty cell — kColorClear alpha is 0 (matches the gather's discard test).
    // Degenerate the whole instance off-screen so it produces no fragments.
    if (color.a < 0.1) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColor = vec4(0.0);
        vDepth = 1.0;
        vIsoDepth = 0.0;    // unused (discarded in fragment)
        vDepthColorMode = 0;
        vDepthColorExtent = 0.0;
        vQuadParam = vec2(0.5);
        vMarginDepthBias = 0.0;
        return;
    }

    const int rawDist = texelFetch(triangleDistances, ij, 0).r;
    // Per-axis fractional encoding (#1458): (depth << 10) | (uFrac4 << 6) | (vFrac4 << 2) | slot
    const int slot = rawDist & 3;
    const int vFrac4 = (rawDist >> 2) & 15;
    const int uFrac4 = (rawDist >> 6) & 15;
    const int rawDepth = rawDist >> 10;      // pos3DtoDistance of the face origin (world units)
    const int faceId = visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Recover the exact face origin from the un-yawed (cardinal) iso store. The
    // store filed this face at `perAxisBase + pos3DtoPos2DIso(facePos)`, so the
    // cardinal iso pixel is `ij - perAxisBase` and isoPixelToPos3D inverts it
    // exactly against rawDepth (= x+y+z of the face plane). Non-singular at every
    // yaw because the recovered index is UN-yawed; the live yaw is applied below
    // by pos3DtoPos2DIsoYawed. See c_voxel_to_trixel_stage_1.glsl.
    // Hoist in-plane axes so the fractional offset below and the coverage
    // dilation block below both share the same eu/ev without a second call.
    vec3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const ivec2 isoPix = ij - perAxisBase;
    const vec3 baseOrigin = isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
    // Apply sub-cell offset packed in the encoding (#1458).
    const vec3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0 - 0.5)
        + ev * (float(vFrac4) / 16.0 - 0.5);

    // Project the selected face corner under the continuous yaw
    // (pos3DtoPos2DIsoYawed is linear, so this IS P(theta)*corner — the true
    // deformed footprint, with no gather / parity inverse). `origin` is already
    // the face plane, so only the in-plane axes are spanned (no polarity).
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    const vec2 cornerIso = vec2(perAxisBase) + pos3DtoPos2DIsoYawed(worldCorner, visualYaw);

    // Inverse of the gather's aPos->canvasPixel map (v_trixel_to_framebuffer):
    //   canvasPixel = (aPos.x + 0.5, -aPos.y + 0.5) * canvasSize
    // so the scatter lands at the same screen scale/offset as the fast path.
    vec2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5;
    quadPos.y = 0.5 - cornerIso.y / float(canvasSize.y);
    vec4 clipCorner = mpMatrix * vec4(quadPos, 1.0, 1.0);
    // Conservative screen-space coverage (#1494): grow the quad outward along its
    // two screen edge normals so a sub-pixel-thin deformed rhombus still covers a
    // fragment center. Same shared bug as the detached scatter — on the large
    // world canvas the gaps are usually sub-pixel, but they surface on small
    // foreshortened faces. The face's in-plane unit axes map (linearly) through
    // the same canvas-normalize -> mpMatrix chain as the corner above.
    const vec2 fbRes = max(scatterFbResolution.xy, vec2(1.0));
    const vec2 ndcPerPx = vec2(2.0) / fbRes;
    const vec2 pxPerNdc = fbRes * 0.5;
    vec2 isoEu = pos3DtoPos2DIsoYawed(eu, visualYaw);
    vec2 isoEv = pos3DtoPos2DIsoYawed(ev, visualYaw);
    vec2 quadEu = vec2(isoEu.x / float(canvasSize.x), -isoEu.y / float(canvasSize.y));
    vec2 quadEv = vec2(isoEv.x / float(canvasSize.x), -isoEv.y / float(canvasSize.y));
    vec2 su = (mpMatrix * vec4(quadEu, 0.0, 0.0)).xy * pxPerNdc;
    vec2 sv = (mpMatrix * vec4(quadEv, 0.0, 0.0)).xy * pxPerNdc;
    // Per-axis continuous conservative margin (#1883). The helper derives a
    // per-edge margin from each axis's own on-screen extent (floored at
    // kScatterDilateMarginPx), so the collapsing axis grows to bridge the band
    // gap while the long silhouette edge stays tight. Replaces the anisotropic
    // max(suLen,svLen) + hard degenSin gate that over-grew the long axis and
    // dashed the foreshortened silhouette edge. The margin-depth bias still keeps
    // any grown margin yielding to a real exact footprint (interior overlap is
    // harmless; only genuine inter-sliver gaps fill).
    const vec2 dilNdc = scatterConservativeDilation(
        su, sv, sign(aPos), kScatterDilateMarginPx, ndcPerPx);
    clipCorner.xy += dilNdc;
    gl_Position = clipCorner;

    vColor = color;
    // Face-center iso-depth for depth-color (#1697). Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance, so
    // interpolation would be a no-op anyway and flat avoids shader-pipeline
    // divergence from adding a smooth varying.
    vIsoDepth = origin.x + origin.y + origin.z;
    vDepthColorMode = depthColorMode;
    vDepthColorExtent = depthColorExtent;
    if (scatterDebugMode == kOverlayPerAxisId) {
        vColor = vec4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
    } else if (scatterDebugMode == kOverlayPerAxisOrigin) {
        // Cell-parity brightness modulation: distinguishes WHICH cell's quad
        // covers a pixel (adjacent cells alternate brightness) on top of the
        // recovered-depth hue.
        float cellParity = float((ij.x + ij.y) & 1) * 0.45 + 0.55;
        vColor = vec4(hueWheel(float(rawDepth) / kOriginHuePeriod) * cellParity, 1.0);
    }

    // Express the dilation offset in the face's in-plane (su, sv) basis so the
    // dilated corner's quad-param coords and its planar depth stay EXACT
    // (#1457). Degenerate basis (edge-on face) -> treat the corner as exact;
    // such a sliver's pixels are covered by the other two visible faces.
    const vec2 dilPx = dilNdc * pxPerNdc;
    const float det = su.x * sv.y - su.y * sv.x;
    vec2 dilParam = vec2(0.0);
    if (abs(det) > 1e-6) {
        dilParam = vec2(dilPx.x * sv.y - dilPx.y * sv.x, su.x * dilPx.y - su.y * dilPx.x) / det;
    }
    vQuadParam = cornerSel + dilParam;

    // Yaw-consistent composite depth (#1370), per-fragment PLANAR + exact
    // (#1457). The stored `rawDepth` (= un-yawed world x+y+z) is the
    // face-local origin-recovery KEY and must not change. Each corner emits
    // the continuous yawed camera-space depth of its own (dilated) corner
    // point via the shared scatterCompositeDepthKey helper
    // (ir_iso_common.glsl) — *4 + slot scale, so it co-sorts with the SDF
    // (c_shapes_to_trixel smoothYaw). Linear interpolation then reproduces
    // the face plane's affine depth field at every fragment. The flat
    // per-quad ROUNDED key this replaces had two failure modes at off-snap
    // residuals: integer quantization tied distinct planes (draw order picked
    // the farther quad on the sign-flip side of the bracket), and same-plane
    // neighbor cells carried different flat keys (the nearer cell's dilation
    // margin beat the true owner's interior along every cell boundary) — the
    // #1457 wrong-voxel-color bands. Per-axis is residual-only, so the
    // cardinal fast path is untouched (byte-identical).
    const float kU = scatterCompositeDepthKey(eu, visualYaw, 0);  // gradient only — slot term cancels
    const float kV = scatterCompositeDepthKey(ev, visualYaw, 0);  // gradient only — slot term cancels
    const float cornerKey = scatterCompositeDepthKey(worldCorner, visualYaw, slot) +
                            dilParam.x * kU + dilParam.y * kV;
    const float depthRange = float(kMaxTriangleDistance - kMinTriangleDistance);
    vDepth = (cornerKey + float(distanceOffset - kMinTriangleDistance)) / depthRange;
    vMarginDepthBias = kScatterMarginDepthBiasKey / depthRange;
}
