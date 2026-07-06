// Project: Irreden Engine
// File: metal/peraxis_scatter.metal
// Smooth camera Z-yaw — T3 (#1310) forward-scatter composite (Metal mirror of
// v_/f_peraxis_scatter.glsl). Each instance is one per-axis canvas cell; the
// vertex stage recovers the face origin and projects its true deformed face
// quad, the fragment stage writes color + depth so the framebuffer depth test
// composites the three per-axis canvases.

#include <metal_stdlib>
using namespace metal;

#include "ir_iso_common.metal"

struct VertexIn {
    float2 position [[attribute(0)]];  // unit quad corner in [-0.5, 0.5]^2
};

struct GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with trixel_to_framebuffer.metal (buffer 3); the scatter reads the
// extra perAxisBase / visualYaw / visibleFaceIds the C++ FrameData appends.
struct FrameDataIsoTriangles {
    float4x4 mpMatrix;
    float2 zoomLevel;
    float2 canvasOffset;
    float2 textureOffset;
    float2 mouseHoveredTriangleIndex;
    float2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    int2 perAxisBase;
    float visualYaw;
    int scatterDebugMode; // raw DebugOverlayMode; 4/5 = composite instrumentation (#1457)
    int4 visibleFaceIds;
    // P3b detached fields (unused on the camera path) — declared only to reach
    // scatterFbResolution at the shared std140 offset 176 (#1494).
    float4 _detachedResidualPad;
    float4 _detachedDepthAxisPad;
    float4 scatterFbResolution; // framebuffer .xy for the conservative dilation (#1494)
    // Per-pixel depth-color debug mode (#1697). When depthColorMode != 0 the
    // fragment shader evaluates hue from the interpolated isoDepth instead of
    // color. depthColorExtent is the bounding half-sum used to normalize [0,1].
    // std140-appended at offset 192; only the scatter shaders read it.
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
};

struct VertexOut {
    float4 position [[position]];
    float4 color [[flat]];
    // Per-fragment PLANAR composite depth + margin classification (#1457) —
    // mirror of v_/f_peraxis_scatter.glsl. depth is the face plane's exact
    // depth linearly interpolated (no-perspective, w==1) from per-corner
    // planar keys; quadParam spans the exact footprint on [0,1]^2 with
    // dilated corners landing outside, so the fragment stage can make
    // conservative-dilation margins yield by marginBias instead of letting
    // draw order decide same-plane overlaps (the #1457 wrong-voxel-color
    // bands).
    float depth [[center_no_perspective]];
    float2 quadParam [[center_no_perspective]];
    float marginBias [[flat]];
    // Per-axis margin-yield slope (#1883), vDepth units per unit quad-param
    // penetration — mirror of v_/f_peraxis_scatter.glsl. The fragment stage scales
    // a margin's yield by penetration * slope so a cell-deep margin yields a shared
    // ridge to the neighbor face's exact footprint (the doubled top<->side sliver).
    float marginYieldGradU [[flat]];
    float marginYieldGradV [[flat]];
    // Face-center iso-depth for depth-color (#1697). Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    float isoDepth [[flat]];
    int depthColorMode [[flat]];
    float depthColorExtent [[flat]];
    // Deterministic cell tiebreak (#2255) — mirror of v_peraxis_scatter.glsl's
    // vCellTieOffset: the fragment stage quantizes its final depth to
    // kScatterCellTieBand and injects this 8-level cell code (pre-scaled to
    // kScatterCellTieStep units) into the sub-band bits, so tie-band
    // fragments resolve by cell identity instead of the #1961 compaction's
    // run-variant atomic-append draw order.
    float cellTieOffset [[flat]];
    // Per-edge interior/boundary classification for analytic coverage (#1937) —
    // .x = u-low, .y = u-high, .z = v-low, .w = v-high (in the face's eu/ev basis);
    // 1 = interior (fill solid / close seam), 0 = true silhouette (crisp trim). An
    // edge is interior if the face continues to its same-axis in-plane neighbour OR
    // it points toward a visible perpendicular face (a convex cube edge shared with
    // another visible face — see the vertex stage). Flat: classified once per
    // instance, constant across its quad.
    float4 edgeInterior [[flat]];
};

// Composite-instrumentation overlay modes (#1457) — raw DebugOverlayMode
// values (ir_render_enums.hpp). Both modes recolor the scattered quad and
// leave depth untouched, so the per-pixel depth-test winner is exactly the
// real composite's winner. Mirror of v_peraxis_scatter.glsl.
constant int kOverlayPerAxisId = 4;     // winner identity: X=red, Y=green, Z=blue
constant int kOverlayPerAxisOrigin = 5; // recovered-origin field: hue wheel of rawDepth

// Long-period hue wheel for the recovered-origin overlay — mirror of
// v_peraxis_scatter.glsl. 96 ≈ 12 voxels per revolution at density 8, so a
// clean face reads as a smooth hue progression and a wrong-cell winner as a
// hue discontinuity (no power-of-two aliasing against the micro lattice).
constant float kOriginHuePeriod = 96.0;
static inline float3 hueWheel(float t) {
    t = fract(t);
    return clamp(
        float3(abs(t * 6.0 - 3.0) - 1.0, 2.0 - abs(t * 6.0 - 2.0), 2.0 - abs(t * 6.0 - 4.0)),
        0.0,
        1.0
    );
}

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

// Occupancy of a per-axis canvas cell at pixel `p`, for the #1937 interior/
// boundary edge classification. The bound `triangleColors` holds ONLY this axis's
// faces (each axis binds its own textures — system_trixel_to_framebuffer.hpp), so
// a non-empty neighbour means this face continues to its in-plane neighbour
// (interior edge); an empty or out-of-bounds neighbour is a silhouette (boundary).
static inline float occupiedNeighbor(texture2d<float> tex, int2 p, int2 size) {
    if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y) {
        return 0.0f;
    }
    return (tex.read(uint2(p)).a >= 0.1f) ? 1.0f : 0.0f;
}

// Polarity (+1 / -1) of the visible face for world axis `axisIdx` (0=x,1=y,2=z),
// from the visible-triplet, for the #1937 cross-axis edge classification. The
// camera sees exactly one polarity per axis; an in-plane edge of the current face
// that points toward that visible side face is a CONVEX CUBE EDGE shared with
// another VISIBLE face (in a different per-axis canvas, so the same-axis occupancy
// tap above can't see it). Such an edge is an inter-face seam to CLOSE
// (conservative overlap), not a silhouette to trim — only the opposite,
// background-facing edges are true silhouettes. Returns 0 if the axis has no
// visible face in the triplet (degenerate).
static inline int visiblePolarityForAxis(int axisIdx, int4 visibleFaceIds) {
    for (int s = 0; s < 3; ++s) {
        const int fid = visibleFaceIds[s];
        if ((fid >> 1) == axisIdx) {
            return ((fid & 1) == 1) ? 1 : -1;
        }
    }
    return 0;
}

// In-plane corner of a face whose `origin` ALREADY sits at the face plane on
// the fixed axis (the store bakes the polarity via faceMicroPositionFixed6).
// Spans only the face's two in-plane world axes (X->y,z  Y->x,z  Z->x,y); re-
// adding the polarity offset double-shifts POS faces one cell past the plane —
// the #1310 back-face seam. Mirror of faceSpanCorner in v_peraxis_scatter.glsl.
static inline float3 faceSpanCorner(int axis, float3 origin, float2 cornerSel) {
    if (axis == 0) return origin + float3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + float3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + float3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

vertex VertexOut v_peraxis_scatter(
    VertexIn in [[stage_in]],
    uint instanceId [[instance_id]],
    texture2d<float> triangleColors [[texture(0)]],
    texture2d<int> triangleDistances [[texture(1)]],
    constant GlobalConstants& globals [[buffer(1)]],
    constant FrameDataIsoTriangles& frameData [[buffer(3)]],
    // Per-axis empty-cell compaction (#1961): this axis's occupied-cell linear
    // indices, bound at offset 0 for the axis. The composite draws only occupied
    // cells via indirect instanced draw, so the instance id indexes this list.
    device const uint* compactedCells [[buffer(25)]]
) {
    VertexOut out;
    const int2 canvasSize = int2(triangleColors.get_width(), triangleColors.get_height());
    const int cell = int(compactedCells[instanceId]);
    const uint2 ij = uint2(uint(cell % canvasSize.x), uint(cell / canvasSize.x));

    const float4 color = triangleColors.read(ij);
    if (color.a < 0.1f) {
        out.position = float4(2.0, 2.0, 2.0, 1.0);
        out.color = float4(0.0);
        out.depth = 1.0;
        out.isoDepth = 0.0;
        out.depthColorMode = 0;
        out.depthColorExtent = 0.0;
        out.quadParam = float2(0.5);
        out.marginBias = 0.0;
        out.marginYieldGradU = 0.0;
        out.marginYieldGradV = 0.0;
        out.cellTieOffset = 0.0;
        out.edgeInterior = float4(0.0);
        return out;
    }

    const int rawDist = triangleDistances.read(ij).r;
    // Per-axis fractional encoding (#1458): (depth << 10) | (uFrac4 << 6) | (vFrac4 << 2) | slot
    const int slot = rawDist & 3;
    const int vFrac4 = (rawDist >> 2) & 15;
    const int uFrac4 = (rawDist >> 6) & 15;
    const int rawDepth = rawDist >> 10;      // pos3DtoDistance of the face origin (world units)
    const int faceId = frameData.visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Hoist in-plane axes before origin so fractional offset and dilation block share them.
    float3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    // Un-yawed iso recovery — mirror of v_peraxis_scatter.glsl. The
    // store filed this face at `perAxisBase + pos3DtoPos2DIso(facePos)`, so the
    // cardinal iso pixel is `ij - perAxisBase` and isoPixelToPos3D inverts it
    // exactly against rawDepth (= x+y+z of the face plane). Non-singular at every
    // yaw because the recovered index is un-yawed; the yaw is applied below.
    const int2 isoPix = int2(ij) - frameData.perAxisBase;
    const float3 baseOrigin =
        isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
    // Apply sub-cell offset packed in the encoding (#1458).
    const float3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0f - 0.5f)
        + ev * (float(vFrac4) / 16.0f - 0.5f);

    // Interior/boundary classification for the analytic coverage (#1937). An edge
    // is INTERIOR (fill solid, close the seam) if EITHER:
    //  (1) the face continues to its same-axis in-plane neighbour — a unit in-plane
    //      world step projects to the integer iso offset pos3DtoPos2DIso(eu/ev)
    //      (linear, so the cell's per-axis pixel is ij ± step); tap THIS axis's
    //      colour texture there, OR
    //  (2) the edge points toward the VISIBLE perpendicular face — a convex cube
    //      edge shared with another visible face in a different per-axis canvas
    //      (the same-axis tap can't see it). Exactly one of the ±eu / ±ev edges
    //      faces each visible side face; the opposite, background-facing edges
    //      stay BOUNDARY and get crisply trimmed (true silhouette, no #1883 spike).
    // The polarity-interior edge of each axis SKIPS its occupancy tap — it is
    // interior unconditionally, so the tap result is irrelevant (max with 1.0).
    // That halves the per-vertex texture reads (2 taps, not 4) on this hot per-cell
    // path while staying output-identical to the max(tap, polarity) form.
    const int2 stepU = pos3DtoPos2DIso(int3(eu));
    const int2 stepV = pos3DtoPos2DIso(int3(ev));
    const int euAxis = (eu.x != 0.0f) ? 0 : ((eu.y != 0.0f) ? 1 : 2);
    const int evAxis = (ev.x != 0.0f) ? 0 : ((ev.y != 0.0f) ? 1 : 2);
    const int euPol = visiblePolarityForAxis(euAxis, frameData.visibleFaceIds);
    const int evPol = visiblePolarityForAxis(evAxis, frameData.visibleFaceIds);
    out.edgeInterior = float4(
        (euPol < 0) ? 1.0f : occupiedNeighbor(triangleColors, int2(ij) - stepU, canvasSize),  // u-low  (-eu)
        (euPol > 0) ? 1.0f : occupiedNeighbor(triangleColors, int2(ij) + stepU, canvasSize),  // u-high (+eu)
        (evPol < 0) ? 1.0f : occupiedNeighbor(triangleColors, int2(ij) - stepV, canvasSize),  // v-low  (-ev)
        (evPol > 0) ? 1.0f : occupiedNeighbor(triangleColors, int2(ij) + stepV, canvasSize)); // v-high (+ev)

    const float2 cornerSel = in.position + float2(0.5);
    const float3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    const float2 cornerIso =
        float2(frameData.perAxisBase) + pos3DtoPos2DIsoYawed(worldCorner, frameData.visualYaw);

    float2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5f;
    quadPos.y = 0.5f - cornerIso.y / float(canvasSize.y);
    float4 clipCorner = frameData.mpMatrix * float4(quadPos, 1.0, 1.0);
    // Conservative screen-space coverage (#1494) — mirror of v_peraxis_scatter.glsl.
    // Grow the quad outward along its two screen edge normals so a sub-pixel-thin
    // deformed rhombus still covers a fragment center. Pre-y-flip clip space.
    const float2 fbRes = max(frameData.scatterFbResolution.xy, float2(1.0));
    const float2 ndcPerPx = float2(2.0) / fbRes;
    const float2 pxPerNdc = fbRes * 0.5;
    float2 isoEu = pos3DtoPos2DIsoYawed(eu, frameData.visualYaw);
    float2 isoEv = pos3DtoPos2DIsoYawed(ev, frameData.visualYaw);
    float2 quadEu = float2(isoEu.x / float(canvasSize.x), -isoEu.y / float(canvasSize.y));
    float2 quadEv = float2(isoEv.x / float(canvasSize.x), -isoEv.y / float(canvasSize.y));
    float2 su = (frameData.mpMatrix * float4(quadEu, 0.0, 0.0)).xy * pxPerNdc;
    float2 sv = (frameData.mpMatrix * float4(quadEv, 0.0, 0.0)).xy * pxPerNdc;
    // Visit-bound dilation (#1937, Metal-lead). scatterConservativeDilation now
    // grows each edge by a FIXED kScatterDilateMarginPx (~1px) — just enough that
    // the rasterizer VISITS every fragment the true footprint could touch. The
    // coverage DECISION moved to f_peraxis_scatter (analytic, from quadParam +
    // edgeInterior), so the old per-axis continuous margin (#1883) that *decided*
    // coverage is retired here. The GL twin (v_peraxis_scatter.glsl) still carries
    // that old coverage role; it ports to this visit-bound in #1938.
    const float2 dilNdc = scatterConservativeDilation(
        su, sv, sign(in.position), kScatterDilateMarginPx, ndcPerPx
    );
    clipCorner.xy += dilNdc;
    clipCorner.y = -clipCorner.y;
    out.position = clipCorner;

    out.color = color;
    // Face-center iso-depth for depth-color (#1697). Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    out.isoDepth = origin.x + origin.y + origin.z;
    out.depthColorMode = frameData.depthColorMode;
    out.depthColorExtent = frameData.depthColorExtent;
    if (frameData.scatterDebugMode == kOverlayPerAxisId) {
        out.color = float4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
    } else if (frameData.scatterDebugMode == kOverlayPerAxisOrigin) {
        // Cell-parity brightness modulation — mirror of v_peraxis_scatter.glsl.
        const float cellParity = float((ij.x + ij.y) & 1u) * 0.45f + 0.55f;
        out.color = float4(hueWheel(float(rawDepth) / kOriginHuePeriod) * cellParity, 1.0);
    }

    // Express the dilation offset in the face's in-plane (su, sv) basis so the
    // dilated corner's quad-param coords and its planar depth stay EXACT
    // (#1457) — mirror of v_peraxis_scatter.glsl. Degenerate basis (edge-on
    // face) -> treat the corner as exact; such a sliver's pixels are covered
    // by the other two visible faces.
    const float2 dilPx = dilNdc * pxPerNdc;
    const float det = su.x * sv.y - su.y * sv.x;
    float2 dilParam = float2(0.0);
    if (abs(det) > 1e-6f) {
        dilParam =
            float2(dilPx.x * sv.y - dilPx.y * sv.x, su.x * dilPx.y - su.y * dilPx.x) / det;
    }
    out.quadParam = cornerSel + dilParam;

    // Yaw-consistent composite depth (#1370), per-fragment PLANAR + exact
    // (#1457) — mirror of v_peraxis_scatter.glsl (see that file for the full
    // rationale). `rawDepth` stays the origin-recovery key; each corner emits
    // the continuous yawed depth of its own (dilated) corner point via the
    // shared scatterCompositeDepthKey helper (ir_iso_common.metal), so linear
    // interpolation reproduces the face plane's affine depth field per
    // fragment. Per-axis is residual-only -> cardinal fast path
    // byte-identical.
    // Subdivided composite-depth scale (#1884 high-zoom fix) — mirror of the GLSL.
    // The SDF floor + cardinal gather encode iso-depth SUBDIVIDED (×effSub); the
    // per-axis store is BASE-resolution (#1458), so lift the scatter iso-depth to
    // the same subdivided magnitude (effSub via effectiveSubdivisionsForHover.x) or
    // the floor out-scales the voxels ~effSub× at high zoom and clips them. Scale
    // only the iso-depth (×4) term, not the slot tiebreak; worldCorner keeps its
    // #1458 sub-cell offset so precision is preserved.
    const float subScale = max(frameData.effectiveSubdivisionsForHover.x, 1.0f);
    const float kU = yawedIsoDistance(eu, frameData.visualYaw) * (4.0f * subScale);  // gradient (no slot)
    const float kV = yawedIsoDistance(ev, frameData.visualYaw) * (4.0f * subScale);  // gradient (no slot)
    const float cornerKey = yawedIsoDistance(worldCorner, frameData.visualYaw) * (4.0f * subScale) +
                            float(slot) + dilParam.x * kU + dilParam.y * kV;
    const float depthRange =
        float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    out.depth =
        (cornerKey + float(frameData.distanceOffset - globals.kMinTriangleDistance)) / depthRange;
    out.marginBias = kScatterMarginDepthBiasKey * subScale / depthRange;
    // Deterministic cell tiebreak (#2255) — mirror of v_peraxis_scatter.glsl:
    // 8 levels, distinct for every same-plane / parallel-plane neighbor pair.
    out.cellTieOffset =
        float((ij.x & 1u) | ((ij.y & 3u) << 1u)) * kScatterCellTieStep;
    // Per-axis margin-yield slope (#1883) — mirror of v_peraxis_scatter.glsl.
    // kU/kV are the per-unit-axis composite depth gradients; scaled to vDepth units
    // and pre-absed (penetration is always outward) and folded with
    // kScatterMarginYieldGradScale so the fragment stage adds penetration*slope as
    // the over-grown margin's extrapolation-proportional yield.
    out.marginYieldGradU = kScatterMarginYieldGradScale * abs(kU) / depthRange;
    out.marginYieldGradV = kScatterMarginYieldGradScale * abs(kV) / depthRange;
    return out;
}

// HSV → RGB. Identical to hsvToRgb in c_shapes_to_trixel.metal — voxel-scatter
// depth-color is bit-exact with the SDF twin when mode is on (#1697).
static inline float3 hsvToRgb(float3 c) {
    const float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    const float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

fragment FragmentOut f_peraxis_scatter(VertexOut in [[stage_in]]) {
    FragmentOut out;
    if (in.color.a < 0.1f) {
        discard_fragment();
    }
    // Analytic edge-aware coverage (#1937, epic #1933 root fix). The visit-bound
    // dilation now only guarantees this fragment was VISITED; the coverage DECISION
    // is here, from the fragment's position in the true [0,1]^2 footprint
    // (in.quadParam) and its per-edge interior/boundary flags. Hard-thresholded for
    // the R32I/depth co-sort write (no alpha blend). This removes the #1883
    // near-cardinal corner spikes and foreshortened-silhouette dashing at the
    // source. fwidth() before any non-uniform discard so the derivative is valid
    // (the alpha test above is on a flat varying — uniform across the instance).
    const float coverage = scatterAnalyticEdgeCoverage(
        in.quadParam, fwidth(in.quadParam), in.edgeInterior);
    if (coverage < 0.5f) {
        discard_fragment();
    }
    if (in.depthColorMode != 0) {
        float dColor = in.depthColorExtent;
        float denomC = max((4.0f / 3.0f) * dColor, 1.0f);
        float t = clamp((in.isoDepth + dColor) / denomC, 0.0f, 1.0f);
        out.color = float4(hsvToRgb(float3(0.66f * t, 1.0f, 1.0f)), 1.0f);
    } else {
        out.color = in.color;
    }
    // Margin-yield (#1457): fragments outside the exact [0,1]^2 footprint are
    // conservative-dilation margin and only fill pixels no exact footprint
    // claims — mirror of f_peraxis_scatter.glsl.
    const bool inMargin = any(in.quadParam < float2(0.0)) || any(in.quadParam > float2(1.0));
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope so a
    // cell-deep margin yields the shared ridge to the neighbor face's exact
    // footprint while a sub-pixel gap-fill still wins (#1883) — mirror of
    // f_peraxis_scatter.glsl.
    const float2 outside = max(max(-in.quadParam, in.quadParam - float2(1.0)), float2(0.0));
    const float yieldBias =
        in.marginBias + outside.x * in.marginYieldGradU + outside.y * in.marginYieldGradV;
    // #2255: band-quantize + cell-code injection — mirror of
    // f_peraxis_scatter.glsl (exact power-of-two float ops on both backends).
    const float scatterDepth = in.depth + (inMargin ? yieldBias : 0.0f);
    out.depth =
        floor(scatterDepth / kScatterCellTieBand) * kScatterCellTieBand + in.cellTieOffset;
    return out;
}
