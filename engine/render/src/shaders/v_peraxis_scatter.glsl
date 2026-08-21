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

// Per-axis empty-cell compaction (#1961): this axis's occupied-cell linear
// indices, bindRange'd so index 0 is the axis region base. The composite draws
// only occupied cells via indirect instanced draw, so gl_InstanceID indexes the
// compacted list rather than sweeping the whole worst-case grid. SSBO binding 25
// is a separate namespace from the sampler/UBO bindings, so it does not collide.
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};

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
    // View-visibility overflow lane draw selector (#2333). 0 = the per-cell
    // scatter (instancing over the compacted occupied cells). 1 = the overflow
    // entry draw drawPerAxisScatter issues after the three cell draws: binding
    // 25 then holds the appended {iso cell, colorPacked, encoded distance}
    // entries and gl_InstanceID indexes entries, not cells. std140-appended
    // (offset 208) so every prior offset is unchanged.
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
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
// Per-axis margin-yield slope (#1883): kScatterMarginYieldGradScale * |depth
// gradient| per unit of in-plane quad-param, in vDepth units. The fragment stage
// multiplies these by the fragment's penetration past the exact [0,1]^2 footprint
// to grow the margin yield in proportion to its plane-extrapolation excursion, so
// a cell-deep margin yields a shared ridge to the neighbor face's exact footprint
// (the doubled top<->side sliver) while a sub-pixel gap-fill barely yields.
flat out float vMarginYieldGradU;
flat out float vMarginYieldGradV;
// Interior-edge yield-slope floor (#2428), in vDepth units per unit quad-param
// penetration. The per-axis slopes above are the OWN plane's depth gradients —
// near zero along a foreshortened axis — but a margin that penetrates an
// INTERIOR edge extends over the ADJACENT visible face, whose plane can diverge
// from the extrapolation at up to 2*sqrt(2)*encScale per world unit. At
// fractional offsets the sub-pixel phase then tips the near-balanced
// margin-vs-exact contest per pixel — the #2428 shared-edge fringe. Flooring the
// slope at kScatterMarginYieldGradScale * encScale (>= the divergence bound) for
// interior-edge penetration makes such margins always lose to the adjacent
// face's exact fragments; they keep only their gap-fill job. Boundary
// (silhouette) penetrations keep the tighter own-slope yield.
flat out float vMarginYieldGradFloor;
// Flat interior-edge yield (#2428): covers the constant (flip << 2) | slot
// key-tiebreak span between adjacent faces' planes — the penetration-independent
// advantage a sub-pixel interior margin can hold over the adjacent face's exact
// fragments (see kScatterMarginInteriorBiasKey in ir_iso_common.glsl).
flat out float vMarginInteriorYieldBias;
// Per-edge interior/boundary classification for the analytic coverage (#1937) —
// .x = u-low, .y = u-high, .z = v-low, .w = v-high (in the face's eu/ev basis);
// 1 = interior (fill solid / close seam), 0 = true silhouette (crisp trim). An
// edge is interior if the face continues to its same-axis in-plane neighbour OR
// it points toward a visible perpendicular face (a convex cube edge shared with
// another visible face — see main()). Flat: classified once per instance,
// constant across its quad.
flat out vec4 vEdgeInterior;
// Face-center iso-depth for per-face depth-color (#1697). Flat (constant across
// the quad) — origin is the same for all 4 corners of a face instance, so
// interpolation would be a no-op anyway and flat avoids shader-pipeline
// divergence from adding a smooth varying.
flat out float vIsoDepth;
flat out int vDepthColorMode;
flat out float vDepthColorExtent;
// Deterministic sub-band tiebreak (#2255/#2411): the fragment stage
// quantizes its final depth to kScatterCellTieBand and injects this 4-bit
// priority-major code — (rank2 << 2) | cell2, pre-scaled to
// kScatterCellTieStep units — into the sub-band bits. UNFLIPPED cross-axis
// band ties (distinct slots by construction) resolve by slot rank,
// consistently along the whole plane-crossing strip; same-slot ties — the
// #2255 margin-yield crossover between parallel neighbor faces — fall to cell
// identity instead of draw order (the #1961 compaction's atomic-append
// instance order is run-variant). Cross-axis flipped-vs-flipped pairs both
// collapse to rank 3 and are NOT proven distinct — a rare residual of the same
// #2255 class. See kScatterCellTieStep in ir_iso_common.glsl for the full
// layout, the per-class separation argument, and the two-sided band
// precondition.
flat out float vCellTieOffset;

// Composite-instrumentation overlay modes (#1457) — raw DebugOverlayMode
// values (ir_render_enums.hpp). Both modes recolor the scattered quad and
// leave vDepth untouched, so the per-pixel depth-test winner is exactly the
// real composite's winner.
const int kOverlayPerAxisId = 4;     // winner identity: X=red, Y=green, Z=blue
const int kOverlayPerAxisOrigin = 5; // recovered-origin field: hue wheel of rawDepth
// Margin-classification overlay (#2428): axis hue, brightened per-fragment by
// the margin test in f_peraxis_scatter (signaled via the vDepthColorMode = -1
// sentinel — the depth-color UBO field is never negative on the normal path).
const int kOverlayPerAxisMargin = 7;

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
// Occupancy of a per-axis canvas cell at pixel `p`, for the #1937 interior/
// boundary edge classification. The bound `triangleColors` holds ONLY this axis's
// faces (each axis binds its own textures — system_trixel_to_framebuffer.hpp), so
// a non-empty neighbour means this face continues to its in-plane neighbour
// (interior edge); an empty or out-of-bounds neighbour is a silhouette (boundary).
float occupiedNeighbor(ivec2 p, ivec2 size) {
    if (p.x < 0 || p.y < 0 || p.x >= size.x || p.y >= size.y) {
        return 0.0;
    }
    return (texelFetch(triangleColors, p, 0).a >= 0.1) ? 1.0 : 0.0;
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
int visiblePolarityForAxis(int axisIdx) {
    for (int s = 0; s < 3; ++s) {
        const int fid = visibleFaceIds[s];
        if ((fid >> 1) == axisIdx) {
            return ((fid & 1) == 1) ? 1 : -1;
        }
    }
    return 0;
}

vec3 faceSpanCorner(int axis, vec3 origin, vec2 cornerSel) {
    if (axis == 0) return origin + vec3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + vec3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + vec3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

void main() {
    const ivec2 canvasSize = textureSize(triangleDistances, 0);
    ivec2 ij;
    vec4 color;
    int rawDist;
    if (overflowMode != 0) {
        // View-visibility overflow lane (#2333): this instance is an appended
        // entry carrying the exact (cardinal cell, encoded distance) pair the
        // store would have written for a view-visible face the per-cell store
        // dropped, plus its raw voxel color (albedo-only in this child —
        // lighting is #2334). Everything below is bit-identical to the cell
        // path; only the data source differs.
        const uint entryBase = uint(gl_InstanceID) * 3u;
        const uint packedCell = compactedCells[entryBase + 0u];
        ij = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
        color = unpackColor(compactedCells[entryBase + 1u]);
        rawDist = int(compactedCells[entryBase + 2u]);
    } else {
        // #1961: the compaction pre-pass appended this axis's occupied cells,
        // so the instance id indexes the compacted list (this axis bindRange'd
        // to base 0) instead of enumerating every worst-case grid cell.
        const int cell = int(compactedCells[gl_InstanceID]);
        ij = ivec2(cell % canvasSize.x, cell / canvasSize.x);
        color = texelFetch(triangleColors, ij, 0);
        rawDist = texelFetch(triangleDistances, ij, 0).r;
    }
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
        vCellTieOffset = 0.0;
        vEdgeInterior = vec4(0.0);
        return;
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
    // The stored plane origin already sits on the flipped plane (the store
    // bakes polarity via faceMicroPositionFixed6 and (pixel, depth) inverts
    // exactly), and the two polarities share their in-plane span axes — so
    // recovery below is unchanged; only faceId itself flips (slot-key + the
    // debug overlays stay exact).
    const int faceId = visibleFaceIds[slot] ^ flip;
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
    // Apply the sub-cell offsets packed in the encoding (#1458): u/v shift
    // within the face plane; w moves the plane itself along the face axis —
    // without it every fractionally-positioned face snaps to the integer
    // lattice plane and the entity's faces stop meeting at shared edges.
    const vec3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0 - 0.5)
        + ev * (float(vFrac4) / 16.0 - 0.5)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0 - 0.5);

    // Interior/boundary classification for the analytic coverage (#1937). An edge
    // is INTERIOR (fill solid, close the seam) if EITHER:
    //  (1) the face continues to its same-axis in-plane neighbour — a unit in-plane
    //      world step projects to the integer iso offset pos3DtoPos2DIso(eu/ev)
    //      (linear, so the cell's per-axis pixel is ij +/- step); tap THIS axis's
    //      colour texture there, OR
    //  (2) the edge points toward the VISIBLE perpendicular face — a convex cube
    //      edge shared with another visible face in a different per-axis canvas
    //      (the same-axis tap can't see it). Exactly one of the +/-eu / +/-ev edges
    //      faces each visible side face; the opposite, background-facing edges
    //      stay BOUNDARY and get crisply trimmed (true silhouette, no #1883 spike).
    // The polarity-interior edge of each axis SKIPS its occupancy tap — it is
    // interior unconditionally, so the tap result is irrelevant (max with 1.0).
    // That halves the per-vertex texture reads (2 taps, not 4) on this hot per-cell
    // path while staying output-identical to the max(tap, polarity) form.
    if (overflowMode != 0) {
        // #2333: overflow entries are isolated revealed slivers, and the bound
        // triangleColors is whichever axis drew last (the overflow draw is
        // axis-agnostic), so the same-axis occupancy taps below would read a
        // foreign axis's cells. Classify every edge as boundary: the analytic
        // coverage then trims the exact footprint, which tiles gap-free against
        // neighbouring faces' exact footprints in world space.
        vEdgeInterior = vec4(0.0);
    } else {
        const ivec2 stepU = pos3DtoPos2DIso(ivec3(eu));
        const ivec2 stepV = pos3DtoPos2DIso(ivec3(ev));
        const int euAxis = (eu.x != 0.0) ? 0 : ((eu.y != 0.0) ? 1 : 2);
        const int evAxis = (ev.x != 0.0) ? 0 : ((ev.y != 0.0) ? 1 : 2);
        const int euPol = visiblePolarityForAxis(euAxis);
        const int evPol = visiblePolarityForAxis(evAxis);
        vEdgeInterior = vec4(
            (euPol < 0) ? 1.0 : occupiedNeighbor(ij - stepU, canvasSize),  // u-low  (-eu)
            (euPol > 0) ? 1.0 : occupiedNeighbor(ij + stepU, canvasSize),  // u-high (+eu)
            (evPol < 0) ? 1.0 : occupiedNeighbor(ij - stepV, canvasSize),  // v-low  (-ev)
            (evPol > 0) ? 1.0 : occupiedNeighbor(ij + stepV, canvasSize)); // v-high (+ev)
    }

    // Project the selected face corner under the continuous yaw
    // (pos3DtoPos2DIsoYawed is linear, so this IS P(theta)*corner — the true
    // deformed footprint, with no gather / parity inverse). `origin` is already
    // the face plane, so only the in-plane axes are spanned (no polarity).
    // Cell-anchor projection (#2545): the recovered origin is lower-corner
    // lattice, so the anchored form rotates the face about the authored
    // position instead of orbiting it by the half cell.
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    // Screen re-projection anchor (#2546). `perAxisBase` is the STORE anchor —
    // trixelOriginOffsetZ1(canvasSize) (== canvasSize/2 - (1,1)) + floor(cameraIso).
    // Its (-1,-1) is the trixel grid's sub-pixel LATTICE alignment: a
    // canvas-STORAGE convention the `ij - perAxisBase` recovery above depends on,
    // but NOT a screen offset. The forward scatter emits true face quads (no
    // trixel-grid gather), so that lattice alignment must not ride into the
    // on-screen placement — anchor the re-projection on the canvas geometric
    // CENTER (canvasSize/2), which the model matrix maps to screen center. The
    // shift back from the storage origin to the center is exactly +(1,1)
    // (canvasSize/2 - trixelOriginOffsetZ1(canvasSize)). The cardinal gather's
    // on-screen focus carries no such offset; without this the scatter registered
    // a constant ~1 iso px (per axis, zoom-scaled) off the cardinal frames at
    // every non-cardinal yaw (epic #2544 deviation 3).
    const ivec2 reprojBase = perAxisBase + ivec2(1);
    const vec2 cornerIso =
        vec2(reprojBase) + pos3DtoPos2DIsoYawedCellAnchor(worldCorner, visualYaw);

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
    // Visit-bound dilation (#1937). scatterConservativeDilation grows each edge by
    // a FIXED kScatterDilateMarginPx (~1px) — just enough that the rasterizer
    // VISITS every fragment the true footprint could touch. The coverage DECISION
    // lives in f_peraxis_scatter (analytic, from vQuadParam + vEdgeInterior), so
    // the per-axis continuous margin (#1883) that *decided* coverage is retired.
    const vec2 dilNdc = scatterConservativeDilation(
        su, sv, sign(aPos), kScatterDilateMarginPx, ndcPerPx);
    clipCorner.xy += dilNdc;
    gl_Position = clipCorner;

    vColor = color;
    // Face-center iso-depth for depth-color (#1697). Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance, so
    // interpolation would be a no-op anyway and flat avoids shader-pipeline
    // divergence from adding a smooth varying.
    // Cell-anchor sum (#2545) — keeps the depth-color binning consistent with
    // the authored-lattice depth the composite key below now carries.
    vIsoDepth = origin.x + origin.y + origin.z - 1.5;
    vDepthColorMode = depthColorMode;
    vDepthColorExtent = depthColorExtent;
    if (scatterDebugMode == kOverlayPerAxisId) {
        vColor = vec4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
    } else if (scatterDebugMode == kOverlayPerAxisMargin) {
        // #2428: axis hue; the fragment stage brightens margin fragments and
        // dims exact-footprint ones, keyed on the -1 sentinel below.
        vColor = vec4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
        vDepthColorMode = -1;
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
    // (ir_iso_common.glsl) — *kDepthEncodeShift + slot scale, so it co-sorts with the SDF
    // (c_shapes_to_trixel smoothYaw). Linear interpolation then reproduces
    // the face plane's affine depth field at every fragment. The flat
    // per-quad ROUNDED key this replaces had two failure modes at off-snap
    // residuals: integer quantization tied distinct planes (draw order picked
    // the farther quad on the sign-flip side of the bracket), and same-plane
    // neighbor cells carried different flat keys (the nearer cell's dilation
    // margin beat the true owner's interior along every cell boundary) — the
    // #1457 wrong-voxel-color bands. Per-axis is residual-only, so the
    // cardinal fast path is untouched (byte-identical).
    // Subdivided composite-depth scale (#1884 high-zoom fix). The SDF floor +
    // cardinal voxel gather encode iso-depth SUBDIVIDED (worldDepth × effSub × 8);
    // the per-axis store is BASE-resolution (#1458), so its recovered worldCorner
    // is in world units and scatterCompositeDepthKey is ×1. Lift it to the same
    // subdivided magnitude (effSub, carried in effectiveSubdivisionsForHover.x) so
    // SDF + scattered voxels co-sort at every zoom — otherwise the floor out-scaled
    // the voxels ~effSub× at high zoom and clipped them into the floor. Scale only
    // the iso-depth (×kDepthEncodeShift) term, NOT the slot tiebreak, so slot stays a unit-scale
    // tiebreak comparable to the SDF's face bits. worldCorner carries the #1458
    // sub-cell offset, so this scale-up keeps sub-cell depth precision (no z-fight).
    const float subScale = max(effectiveSubdivisionsForHover.x, 1.0);
    const float encScale = float(kDepthEncodeShift) * subScale;
    const float kU = yawedIsoDistance(eu, visualYaw) * encScale;  // gradient (no slot)
    const float kV = yawedIsoDistance(ev, visualYaw) * encScale;  // gradient (no slot)
    // Tiebreak mirrors the integer encode's low bits ((flip << 2) | slot) so a
    // flipped cell co-sorts exactly where a real cardinal store would land it.
    // Cell-anchor depth (#2545): measured at the corner's authored-lattice
    // world point so voxel and SDF surfaces at one world location co-sort
    // exactly at every residual (they previously disagreed by cos(yaw)+0.5).
    const float cornerKey = yawedIsoDistanceCellAnchor(worldCorner, visualYaw) * encScale +
                            float((flip << 2) | slot) + dilParam.x * kU + dilParam.y * kV;
    const float depthRange = float(kMaxTriangleDistance - kMinTriangleDistance);
    vDepth = (cornerKey + float(distanceOffset - kMinTriangleDistance)) / depthRange;
    // #2333: overflow entries sit two tie bands BEHIND everything else,
    // so an entry can never beat an
    // equal-yawed-depth cell-path face (near the 120°/240° coset-depth
    // degeneracy every coset member ties in view depth — without the bias
    // the sub-band tie arbitration hands ~half the lit surface's pixels to
    // unlit albedo entries, a q1/q2 stipple regression). The entries' job is
    // filling pixels NO cell quad claims (the revealed slivers are
    // background there, far beyond any bias), and any genuinely farther
    // surface is >= one voxel depth step away (~500 bands), so the two-band
    // yield changes nothing else.
    if (overflowMode != 0) {
        vDepth += 2.0 * kScatterCellTieBand;
    }
    vMarginDepthBias = kScatterMarginDepthBiasKey * subScale / depthRange;
    // Deterministic sub-band tiebreak (#2255/#2411): priority-major
    // (rank2 = flip ? 3 : slot), cell-minor (cell2 distinct for every
    // same-plane / parallel-plane neighbor pair — in-plane world steps
    // project to iso-diagonal or (0,+/-2) only). Full layout + rationale at
    // kScatterCellTieStep in ir_iso_common.glsl.
    const int rank2 = (flip != 0) ? 3 : slot;
    const int cell2 = (ij.x & 1) | (ij.y & 2);
    vCellTieOffset = float((rank2 << 2) | cell2) * kScatterCellTieStep;
    // Per-axis margin-yield slope (#1883). kU/kV are the per-unit-axis composite
    // depth gradients; scaled to vDepth units and pre-absed (penetration is always
    // outward) so the fragment stage adds penetration*slope as the over-grown
    // margin's extrapolation-proportional yield. Folds in kScatterMarginYieldGradScale.
    vMarginYieldGradU = kScatterMarginYieldGradScale * abs(kU) / depthRange;
    vMarginYieldGradV = kScatterMarginYieldGradScale * abs(kV) / depthRange;
    // Interior-edge floor (#2428): 3 * encScale >= the 2*sqrt(2)*encScale
    // worst-case cross-face divergence per world unit (quadParam is in world
    // units on the base-resolution store), so an interior-edge margin always
    // yields past the adjacent face's exact fragments.
    vMarginYieldGradFloor = kScatterMarginYieldGradScale * encScale / depthRange;
    // Flat interior-edge yield (#2428) — see kScatterMarginInteriorBiasKey in
    // ir_iso_common.glsl for why 8 key units is forced, and why sitting on that
    // ceiling is sound.
    vMarginInteriorYieldBias = kScatterMarginInteriorBiasKey / depthRange;
}
