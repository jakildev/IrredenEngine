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
    // View-visibility overflow lane draw selector (#2333). 0 = the per-cell
    // scatter (instancing over the compacted occupied cells). 1 = the overflow
    // entry draw drawPerAxisScatter issues after the three cell draws: buffer
    // 25 then holds the appended {iso cell, colorPacked, encoded distance}
    // entries and the instance id indexes entries, not cells. std140-appended
    // (offset 208) so every prior offset is unchanged.
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
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
    // Interior-edge yield-slope floor (#2428), vDepth units per unit quad-param
    // penetration. The per-axis slopes above are the OWN plane's depth
    // gradients — near zero along a foreshortened axis — but a margin that
    // penetrates an INTERIOR edge extends over the ADJACENT visible face,
    // whose plane can diverge from the extrapolation at up to
    // 2*sqrt(2)*encScale per world unit. At fractional offsets the sub-pixel
    // phase then tips the near-balanced margin-vs-exact contest per pixel —
    // the #2428 shared-edge fringe. Flooring the slope at
    // kScatterMarginYieldGradScale * encScale (>= the divergence bound) for
    // interior-edge penetration makes such margins always lose to the
    // adjacent face's exact fragments; they keep only their gap-fill job.
    // Boundary (silhouette) penetrations keep the tighter own-slope yield.
    float marginYieldGradFloor [[flat]];
    // Flat interior-edge yield (#2428): covers the constant (flip << 2) | slot
    // key-tiebreak span between adjacent faces' planes — the
    // penetration-independent advantage a sub-pixel interior margin can hold
    // over the adjacent face's exact fragments (see the vertex stage).
    float marginInteriorYieldBias [[flat]];
    // Face-center iso-depth for depth-color (#1697). Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    float isoDepth [[flat]];
    int depthColorMode [[flat]];
    float depthColorExtent [[flat]];
    // Deterministic sub-band tiebreak (#2255/#2411) — mirror of
    // v_peraxis_scatter.glsl's vCellTieOffset: the fragment stage quantizes
    // its final depth to kScatterCellTieBand and injects this 4-bit
    // priority-major code ((rank2 << 2) | cell2, pre-scaled to
    // kScatterCellTieStep units) into the sub-band bits, so tie-band
    // fragments resolve by slot rank then cell identity instead of the
    // #1961 compaction's run-variant atomic-append draw order. Cross-axis
    // flipped-vs-flipped pairs collapse to rank 3 and fall to cell identity
    // without a distinctness proof — they stay draw-order on collision (the
    // rare #2255 residual). Per-class argument: ir_iso_common.glsl.
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
// Margin-classification overlay (#2428) — mirror of v_peraxis_scatter.glsl:
// axis hue, brightened per-fragment by the margin test (signaled via the
// depthColorMode = -1 sentinel — the UBO field is never negative normally).
constant int kOverlayPerAxisMargin = 7;

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
    int2 ij;
    float4 color;
    int rawDist;
    if (frameData.overflowMode != 0) {
        // View-visibility overflow lane (#2333): this instance is an appended
        // entry carrying the exact (cardinal cell, encoded distance) pair the
        // store would have written for a view-visible face the per-cell store
        // dropped, plus its raw voxel color (albedo-only in this child —
        // lighting is #2334). Everything below is bit-identical to the cell
        // path; only the data source differs.
        const uint entryBase = instanceId * 3u;
        const uint packedCell = compactedCells[entryBase + 0u];
        ij = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
        color = unpackColor(compactedCells[entryBase + 1u]);
        rawDist = int(compactedCells[entryBase + 2u]);
    } else {
        const int cell = int(compactedCells[instanceId]);
        ij = int2(cell % canvasSize.x, cell / canvasSize.x);
        color = triangleColors.read(uint2(ij));
        rawDist = triangleDistances.read(uint2(ij)).r;
    }
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

    // Per-axis fractional encoding (#1458, flip carrier #2207) — decode via the
    // shared ir_iso_common helpers. The frac fields keep their positions.
    const int slot = decodeSlot(rawDist);
    const int vFrac4 = decodeVFrac4PerAxis(rawDist);
    const int uFrac4 = decodeUFrac4PerAxis(rawDist);
    const int wFrac4 = decodeWFrac4PerAxis(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist); // pos3DtoDistance of the face origin (world units)
    // A flipped cell (#2207) is the opposite-polarity face of its slot's axis.
    // The stored plane origin already sits on the flipped plane and the two
    // polarities share their in-plane span axes — recovery is unchanged; only
    // faceId itself flips. See the GLSL twin.
    const int faceId = frameData.visibleFaceIds[slot] ^ flip;
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
    // Apply the sub-cell offsets packed in the encoding (#1458): u/v shift
    // within the face plane; w moves the plane itself along the face axis —
    // without it every fractionally-positioned face snaps to the integer
    // lattice plane and the entity's faces stop meeting at shared edges.
    const float3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0f - 0.5f)
        + ev * (float(vFrac4) / 16.0f - 0.5f)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0f - 0.5f);

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
    if (frameData.overflowMode != 0) {
        // #2333: overflow entries are isolated revealed slivers, and the bound
        // triangleColors is whichever axis drew last (the overflow draw is
        // axis-agnostic), so the same-axis occupancy taps below would read a
        // foreign axis's cells. Classify every edge as boundary: the analytic
        // coverage then trims the exact footprint, which tiles gap-free against
        // neighbouring faces' exact footprints in world space.
        out.edgeInterior = float4(0.0);
    } else {
        const int2 stepU = pos3DtoPos2DIso(int3(eu));
        const int2 stepV = pos3DtoPos2DIso(int3(ev));
        const int euAxis = (eu.x != 0.0f) ? 0 : ((eu.y != 0.0f) ? 1 : 2);
        const int evAxis = (ev.x != 0.0f) ? 0 : ((ev.y != 0.0f) ? 1 : 2);
        const int euPol = visiblePolarityForAxis(euAxis, frameData.visibleFaceIds);
        const int evPol = visiblePolarityForAxis(evAxis, frameData.visibleFaceIds);
        out.edgeInterior = float4(
            (euPol < 0) ? 1.0f : occupiedNeighbor(triangleColors, ij - stepU, canvasSize),  // u-low  (-eu)
            (euPol > 0) ? 1.0f : occupiedNeighbor(triangleColors, ij + stepU, canvasSize),  // u-high (+eu)
            (evPol < 0) ? 1.0f : occupiedNeighbor(triangleColors, ij - stepV, canvasSize),  // v-low  (-ev)
            (evPol > 0) ? 1.0f : occupiedNeighbor(triangleColors, ij + stepV, canvasSize)); // v-high (+ev)
    }

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
    } else if (frameData.scatterDebugMode == kOverlayPerAxisMargin) {
        // #2428: axis hue; the fragment stage brightens margin fragments and
        // dims exact-footprint ones, keyed on the -1 sentinel below.
        out.color = float4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
        out.depthColorMode = -1;
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
    // only the iso-depth (×kDepthEncodeShift) term, not the slot tiebreak;
    // worldCorner keeps its #1458 sub-cell offset so precision is preserved.
    const float subScale = max(frameData.effectiveSubdivisionsForHover.x, 1.0f);
    const float encScale = float(kDepthEncodeShift) * subScale;
    const float kU = yawedIsoDistance(eu, frameData.visualYaw) * encScale;  // gradient (no slot)
    const float kV = yawedIsoDistance(ev, frameData.visualYaw) * encScale;  // gradient (no slot)
    // Tiebreak mirrors the integer encode's low bits ((flip << 2) | slot) so a
    // flipped cell co-sorts exactly where a real cardinal store would land it.
    const float cornerKey = yawedIsoDistance(worldCorner, frameData.visualYaw) * encScale +
                            float((flip << 2) | slot) + dilParam.x * kU + dilParam.y * kV;
    const float depthRange =
        float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    out.depth =
        (cornerKey + float(frameData.distanceOffset - globals.kMinTriangleDistance)) / depthRange;
    // #2333: overflow entries sit two tie bands BEHIND everything else,
    // so an entry can never beat an
    // equal-yawed-depth cell-path face (near the 120°/240° coset-depth
    // degeneracy every coset member ties in view depth — without the bias
    // the sub-band tie arbitration hands ~half the lit surface's pixels to
    // unlit albedo entries, a q1/q2 stipple regression). The entries' job is
    // filling pixels NO cell quad claims (the revealed slivers are
    // background there, far beyond any bias), and any genuinely farther
    // surface is >= one voxel depth step away (~500 bands), so the two-band
    // yield changes nothing else. Mirror of v_peraxis_scatter.glsl.
    if (frameData.overflowMode != 0) {
        out.depth += 2.0f * kScatterCellTieBand;
    }
    out.marginBias = kScatterMarginDepthBiasKey * subScale / depthRange;
    // Deterministic sub-band tiebreak (#2255/#2411) — mirror of
    // v_peraxis_scatter.glsl: priority-major (rank2 = flip ? 3 : slot),
    // cell-minor (cell2 distinct for every same-plane / parallel-plane
    // neighbor pair). Full layout + rationale at kScatterCellTieStep in
    // ir_iso_common.glsl.
    const uint rank2 = (flip != 0) ? 3u : uint(slot);
    const uint cell2 = (ij.x & 1u) | (ij.y & 2u);
    out.cellTieOffset = float((rank2 << 2u) | cell2) * kScatterCellTieStep;
    // Per-axis margin-yield slope (#1883) — mirror of v_peraxis_scatter.glsl.
    // kU/kV are the per-unit-axis composite depth gradients; scaled to vDepth units
    // and pre-absed (penetration is always outward) and folded with
    // kScatterMarginYieldGradScale so the fragment stage adds penetration*slope as
    // the over-grown margin's extrapolation-proportional yield.
    out.marginYieldGradU = kScatterMarginYieldGradScale * abs(kU) / depthRange;
    out.marginYieldGradV = kScatterMarginYieldGradScale * abs(kV) / depthRange;
    // Interior-edge floor (#2428): 3 * encScale >= the 2*sqrt(2)*encScale
    // worst-case cross-face divergence per world unit (quadParam is in world
    // units on the base-resolution store), so an interior-edge margin always
    // yields past the adjacent face's exact fragments.
    out.marginYieldGradFloor = kScatterMarginYieldGradScale * encScale / depthRange;
    // Flat interior-edge yield (#2428): the composite key carries the
    // constant (flip << 2) | slot tiebreak (up to 7 key units), so a margin
    // whose slot ranks lower sits a CONSTANT ~key-scale distance nearer than
    // the adjacent face across the whole shared edge — a sub-pixel
    // penetration times any slope can never repay it (the #2428 fringe's
    // dominant term; the integer row only escapes because lattice-aligned
    // edges keep pixel centers out of dilation reach). 8 key units is the
    // FORCED value, sitting ON its ceiling: strictly above the 7-key low-bits
    // span, and exactly one subdivided depth step (kDepthEncodeShift key units
    // at every subdivision) — not "inside" one. Interior margins still gap-fill
    // against background and genuinely farther surfaces (>> 1 cell), which is
    // what makes the ceiling sound. Bracket + asserts: ir_iso_common.metal and
    // ir_render_types.hpp.
    out.marginInteriorYieldBias = kScatterMarginInteriorBiasKey / depthRange;
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
    // Margin-yield (#1457): fragments outside the exact [0,1]^2 footprint are
    // conservative-dilation margin and only fill pixels no exact footprint
    // claims — mirror of f_peraxis_scatter.glsl.
    const bool inMargin = any(in.quadParam < float2(0.0)) || any(in.quadParam > float2(1.0));
    if (in.depthColorMode == -1) {
        // Margin-classification overlay (#2428) — mirror of
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
    // Penetration past the exact [0,1]^2 footprint (per axis, >= 0). A margin
    // fragment yields by the flat bias PLUS penetration * per-axis yield slope so a
    // cell-deep margin yields the shared ridge to the neighbor face's exact
    // footprint while a sub-pixel gap-fill still wins (#1883) — mirror of
    // f_peraxis_scatter.glsl.
    const float2 outside = max(max(-in.quadParam, in.quadParam - float2(1.0)), float2(0.0));
    // Interior-edge yield floor (#2428): a margin that penetrated an INTERIOR
    // edge is extending over the adjacent visible face — floor its yield
    // slope at the cross-face divergence bound so it always loses to that
    // face's exact fragments (see marginYieldGradFloor). The penetrated side
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
    // The flat interior term (see marginInteriorYieldBias) covers the
    // penetration-INDEPENDENT (flip<<2)|slot key gap between adjacent faces;
    // the floored slope covers the penetration-proportional plane divergence.
    const bool interiorPen = (outside.x > 0.0f && interiorU > 0.5f) ||
                             (outside.y > 0.0f && interiorV > 0.5f);
    const float yieldBias = in.marginBias + outside.x * gradU + outside.y * gradV +
        (interiorPen ? in.marginInteriorYieldBias : 0.0f);
    // #2255: band-quantize + cell-code injection — mirror of
    // f_peraxis_scatter.glsl (exact power-of-two float ops on both backends).
    const float scatterDepth = in.depth + (inMargin ? yieldBias : 0.0f);
    out.depth =
        floor(scatterDepth / kScatterCellTieBand) * kScatterCellTieBand + in.cellTieOffset;
    return out;
}
