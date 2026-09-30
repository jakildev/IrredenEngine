// Metal mirror of v_/f_peraxis_scatter.glsl; keep the twins in lockstep. One
// instance per per-axis canvas cell; the framebuffer depth test is what
// composites the three per-axis canvases, so the fragment depth must be the
// shared composite key.

#include <metal_stdlib>
using namespace metal;

#include "ir_scatter_depth.metal"

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
    int scatterDebugMode; // raw DebugOverlayMode; 4/5/7 = scatter instrumentation overlays
    int4 visibleFaceIds;
    // Detached-scatter fields (unused on the camera path) — declared only to
    // reach scatterFbResolution at the shared std140 offset 176.
    float4 _detachedResidualPad;
    float4 _detachedDepthAxisPad;
    float4 scatterFbResolution; // framebuffer extent and visibility-pass flags
    // Per-pixel depth-color debug mode. When depthColorMode != 0 the fragment
    // shader evaluates hue from isoDepth instead of color. depthColorExtent is
    // the bounding half-sum used to normalize [0,1]. std140 offset 192; only the
    // scatter shaders read it.
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
    // Overflow lane draw selector. 0 = the per-cell
    // scatter (instancing over the compacted occupied cells). 1 = the overflow
    // entry draw drawPerAxisScatter issues after the three cell draws: buffer
    // 25 then holds the appended {iso cell, colorPacked, encoded distance}
    // entries and the instance id indexes entries, not cells. std140 offset 208.
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
};

#include "ir_peraxis_scatter_interface.metal"

// Composite-instrumentation overlay modes — raw DebugOverlayMode
// values (ir_render_enums.hpp). Both modes recolor the scattered quad and
// leave depth untouched, so the per-pixel depth-test winner is exactly the
// real composite's winner. Mirror of v_peraxis_scatter.glsl.
constant int kOverlayPerAxisId = 4;     // winner identity: X=red, Y=green, Z=blue
constant int kOverlayPerAxisOrigin = 5; // recovered-origin field: hue wheel of rawDepth
// The margin overlay remains an axis view; exact finite faces use its dim tint.
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

// In-plane corner of a face whose `origin` ALREADY sits at the face plane on
// the fixed axis (the store bakes the polarity via faceMicroPositionFixed6).
// Spans only the face's two in-plane world axes (X->y,z  Y->x,z  Z->x,y);
// adding a polarity offset double-shifts POS faces one cell past the plane —
// a back-face seam. Mirror of faceSpanCorner in v_peraxis_scatter.glsl.
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
    // This axis's occupied-cell linear indices, bound at offset 0 for the axis.
    // The indirect instanced draw covers only occupied cells, so the instance id
    // indexes this list.
    device const uint* compactedCells [[buffer(25)]]
) {
    VertexOut out;
    out.visibilityExtent = int3(frameData.scatterFbResolution.xyz);
    const int2 canvasSize = int2(triangleColors.get_width(), triangleColors.get_height());
    int2 ij;
    float4 color;
    int rawDist;
    if (frameData.overflowMode != 0) {
        // This appended cardinal loser carries the exact (cardinal cell,
        // encoded distance) pair the store would have written, plus its packed
        // color. Everything below is bit-identical to
        // the cell path; only the data source differs.
        const uint entryBase = instanceId * 3u;
        const uint packedCell = compactedCells[entryBase + 0u];
        ij = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
        color = unpackColor(compactedCells[entryBase + 1u]);
        color.a = 1.0f;
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
        out.faceOrigin = float3(0.0);
        out.faceId = 0;
        out.ownerPixel = int2(-1);
        out.depth = 1.0;
        out.isoDepth = 0.0;
        out.depthColorMode = 0;
        out.depthColorExtent = 0.0;
        out.quadParam = float2(0.5);
        out.cellTieOffset = 0.0;
        return out;
    }

    const int slot = decodeSlot(rawDist);
    const int vFrac4 = decodeVFrac4PerAxis(rawDist);
    const int uFrac4 = decodeUFrac4PerAxis(rawDist);
    const int wFrac4 = decodeWFrac4PerAxis(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist); // pos3DtoDistance of the face origin (world units)
    // A flipped cell is the opposite-polarity face of its slot's axis.
    // The stored plane origin already sits on the flipped plane and the two
    // polarities share their in-plane span axes — origin recovery is
    // polarity-independent; only faceId itself flips.
    const int faceId = frameData.visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    float3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    // Un-yawed iso recovery — mirror of v_peraxis_scatter.glsl. The
    // store files this face at `perAxisBase + pos3DtoPos2DIso(facePos)`, so the
    // cardinal iso pixel is `ij - perAxisBase` and isoPixelToPos3D inverts it
    // exactly against rawDepth (= x+y+z of the face plane). Non-singular at every
    // yaw because the recovered index is un-yawed; the yaw is applied only at
    // projection.
    const int2 isoPix = int2(ij) - frameData.perAxisBase;
    const float3 baseOrigin =
        isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
    // Apply the sub-cell offsets packed in the encoding: u/v shift
    // within the face plane; w moves the plane itself along the face axis —
    // without it every fractionally-positioned face snaps to the integer
    // lattice plane and the entity's faces stop meeting at shared edges.
    // Mirror of perAxisSubCellFrac (ir_per_axis_lighting.metal) — this scatter
    // is the PRODUCER that DEFINES the `/16 - 0.5` centring convention the
    // lighting consumers decode; kept inline (not routed through the helper)
    // because pulling ir_per_axis_lighting into this TU is exactly the
    // FP-scheduling perturbation that fragment is separated to avoid.
    const float3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0f - 0.5f)
        + ev * (float(vFrac4) / 16.0f - 0.5f)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0f - 0.5f);

    const float2 cornerSel = in.position + float2(0.5);
    const float3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    // Cell-anchor projection: the recovered origin is lower-corner
    // lattice, so the anchored form rotates the face about the authored
    // position instead of orbiting it by the half cell. Matches the GLSL twin.
    // Screen re-projection anchor: perAxisBase carries
    // trixelOriginOffsetZ1's (-1,-1) sub-pixel LATTICE alignment (a canvas-storage
    // convention the `ij - perAxisBase` recovery needs); the forward scatter emits
    // true face quads, so that must not ride into the screen placement. Anchor on
    // the canvas geometric CENTER instead — the +(1,1) shift back from the storage
    // origin (canvasSize/2 - trixelOriginOffsetZ1(canvasSize)). Anchoring on
    // perAxisBase instead registers the scatter a constant ~1 iso px (per axis)
    // off the cardinal frames at non-cardinal yaw. Matches the GLSL twin.
    const int2 reprojBase = frameData.perAxisBase + int2(1);
    const float2 cornerIso = float2(reprojBase) +
        pos3DtoPos2DIsoYawedCellAnchor(worldCorner, frameData.visualYaw);

    float2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5f;
    quadPos.y = 0.5f - cornerIso.y / float(canvasSize.y);
    float4 clipCorner = frameData.mpMatrix * float4(quadPos, 1.0, 1.0);
    clipCorner.y = -clipCorner.y;
    out.position = clipCorner;

    out.color = color;
    out.faceOrigin = origin;
    out.faceId = faceId;
    out.ownerPixel = frameData.overflowMode != 0 ? int2(-1) : ij;
    // Cell-anchor sum — keeps the depth-color binning consistent with the
    // authored-lattice depth the composite key carries.
    out.isoDepth = origin.x + origin.y + origin.z - 1.5f;
    out.depthColorMode = frameData.depthColorMode;
    out.depthColorExtent = frameData.depthColorExtent;
    if (frameData.scatterDebugMode == kOverlayPerAxisId) {
        out.color = float4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
    } else if (frameData.scatterDebugMode == kOverlayPerAxisMargin) {
        // No margin is drawn; the fragment stage uses the exact-face tint.
        out.color = float4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
        out.depthColorMode = -1;
    } else if (frameData.scatterDebugMode == kOverlayPerAxisOrigin) {
        // Cell-parity brightness modulation — mirror of v_peraxis_scatter.glsl.
        const float cellParity = float((ij.x + ij.y) & 1u) * 0.45f + 0.55f;
        out.color = float4(hueWheel(float(rawDepth) / kOriginHuePeriod) * cellParity, 1.0);
    }

    out.quadParam = cornerSel;

    // Yaw-consistent composite depth, per-fragment PLANAR + exact — mirror of
    // v_peraxis_scatter.glsl. `rawDepth` stays the origin-recovery key; each
    // corner emits the continuous yawed depth of its finite corner point
    // via yawedIsoDistanceCellAnchor, the shared composite depth metric in
    // ir_iso_common.metal, so linear interpolation reproduces the face plane's
    // affine depth field per fragment.
    // Subdivided composite-depth scale — mirror of the GLSL.
    // The SDF floor + cardinal gather encode iso-depth SUBDIVIDED (×effSub); the
    // per-axis store is BASE-resolution, so lift the scatter iso-depth to
    // the same subdivided magnitude (effSub via effectiveSubdivisionsForHover.x) or
    // the floor out-scales the voxels ~effSub× at high zoom and clips them. Scale
    // only the iso-depth (×kDepthEncodeShift) term, not the slot tiebreak;
    // worldCorner keeps its sub-cell offset so precision is preserved.
    const float subScale = max(frameData.effectiveSubdivisionsForHover.x, 1.0f);
    const float encScale = float(kDepthEncodeShift) * subScale;
    // Tiebreak mirrors the integer encode's low bits ((flip << 2) | slot) so a
    // flipped cell co-sorts exactly where a real cardinal store would land it.
    // Cell-anchor depth: measured at the corner's authored-lattice
    // world point so voxel and SDF surfaces at one world location co-sort
    // exactly at every residual. Matches the GLSL twin.
    const float cornerKey = yawedIsoDistanceCellAnchor(worldCorner, frameData.visualYaw) * encScale +
                            float((flip << 2) | slot);
    const float depthRange =
        float(globals.kMaxTriangleDistance - globals.kMinTriangleDistance);
    out.depth =
        (cornerKey + float(frameData.distanceOffset - globals.kMinTriangleDistance)) / depthRange;
    // Overflow entries sit two tie bands BEHIND everything else,
    // so an entry can never beat an
    // equal-yawed-depth cell-path face (near the 120°/240° coset-depth
    // degeneracy every coset member ties in view depth — without the bias
    // the sub-band tie arbitration hands ~half the surface's pixels to
    // overflow entries as a stipple). The entries' job is
    // filling pixels NO cell quad claims (the revealed slivers are
    // background there, far beyond any bias), and any genuinely farther
    // surface is >= one voxel depth step away (~500 bands), so the two-band
    // yield changes nothing else. Mirror of v_peraxis_scatter.glsl.
    if (frameData.overflowMode != 0) {
        out.depth += 2.0f * kScatterCellTieBand;
    }
    // cell2 separates immediate lattice neighbors; displaced cells can collide:
    // in-plane world steps project to iso-diagonal or (0,+/-2) only.
    const uint rank2 = (flip != 0) ? 3u : uint(slot);
    const uint cell2 = (ij.x & 1u) | (ij.y & 2u);
    out.cellTieOffset = float((rank2 << 2u) | cell2) * kScatterCellTieStep;
    return out;
}

#define IR_PER_AXIS_FRAGMENT_NAME f_peraxis_scatter
#include "ir_peraxis_scatter_fragment_body.metal"
