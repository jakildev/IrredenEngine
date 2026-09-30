/*
 * Project: Irreden Engine
 * File: v_peraxis_scatter.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: May 2026
 * -----
 * Smooth camera Z-yaw forward-scatter composite.
 */

#version 450 core

#include "ir_iso_common.glsl"

// Unit quad corner in [-0.5, 0.5]^2 from the shared QuadVAO. (aPos + 0.5)
// gives the {0,1}^2 corner selector for the two in-plane face axes.
layout (location = 0) in vec2 aPos;

layout (binding = 0) uniform sampler2D  triangleColors;
layout (binding = 1) uniform isampler2D triangleDistances;

// This axis's occupied-cell linear indices, bindRange'd so index 0 is the axis region base.
// The indirect instanced draw covers only occupied cells, so gl_InstanceID indexes this
// list. SSBO binding 25 is a separate namespace from the sampler/UBO bindings, so it does
// not collide.
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};

// binding = 1 is shared with the triangleDistances sampler on purpose: in
// GL 4.5 sampler texture-image units and uniform-buffer binding points are
// separate namespaces, so the shared index does not collide.
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with f_/v_trixel_to_framebuffer (binding 3). The cardinal fast path
// reads only the prefix; the per-axis scatter also reads the tail from
// perAxisBase onward.
layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    ivec2 perAxisBase;       // canvas-pixel origin of this axis canvas
    float visualYaw;         // continuous camera Z-yaw (radians)
    int scatterDebugMode;    // raw DebugOverlayMode; 4/5/7 = scatter instrumentation overlays
    ivec4 visibleFaceIds;    // per-slot world FaceId (0..5); .w pad
    // Detached-scatter fields (unused on the camera path) — declared only to
    // reach scatterFbResolution at the shared std140 offset 176.
    vec4 _detachedResidualPad;
    vec4 _detachedDepthAxisPad;
    vec4 scatterFbResolution; // framebuffer extent and visibility-pass flags
    // Per-pixel depth-color debug mode. When depthColorMode != 0 the fragment
    // shader evaluates hue from vIsoDepth instead of vColor. depthColorExtent is
    // the bounding half-sum used to normalize [0,1]. std140 offset 192; only the
    // scatter shaders read it.
    int depthColorMode;
    float depthColorExtent;
    float _depthColorPad0;
    float _depthColorPad1;
    // Overflow lane draw selector. 0 = the per-cell
    // scatter (instancing over the compacted occupied cells). 1 = the overflow
    // entry draw drawPerAxisScatter issues after the three cell draws: binding
    // 25 then holds the appended {iso cell, colorPacked, encoded distance}
    // entries and gl_InstanceID indexes entries, not cells. std140 offset 208.
    int overflowMode;
    int _overflowPad0;
    int _overflowPad1;
    int _overflowPad2;
};

flat out vec4 vColor;
flat out vec3 vFaceOrigin;
flat out int vFaceId;
flat out ivec2 vOwnerPixel;
flat out ivec3 vVisibilityExtent;
// Per-fragment planar composite depth: no-perspective interpolation of the
// yawed depth at each finite face corner reproduces its affine depth field.
noperspective out float vDepth;
// Finite-face in-plane coordinates for surface shadow and lighting queries.
noperspective out vec2 vQuadParam;
// Face-center iso-depth for per-face depth-color. Flat (constant across
// the quad) — origin is the same for all 4 corners of a face instance, so
// interpolation would be a no-op anyway and flat avoids shader-pipeline
// divergence from adding a smooth varying.
flat out float vIsoDepth;
flat out int vDepthColorMode;
flat out float vDepthColorExtent;
// Face/cell priority within a depth band.
flat out float vCellTieOffset;

// Composite-instrumentation overlay modes — raw DebugOverlayMode
// values (ir_render_enums.hpp). Both modes recolor the scattered quad and
// leave vDepth untouched, so the per-pixel depth-test winner is exactly the
// real composite's winner.
const int kOverlayPerAxisId = 4;     // winner identity: X=red, Y=green, Z=blue
const int kOverlayPerAxisOrigin = 5; // recovered-origin field: hue wheel of rawDepth
// The margin overlay remains an axis view; exact finite faces use its dim tint.
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
// Z->x,y). Adding a per-faceId polarity offset here double-shifts POS faces one
// cell past the plane: a ~1px dark back-face seam between the POS face and its
// neighbors. cornerSel in {0,1}^2.
vec3 faceSpanCorner(int axis, vec3 origin, vec2 cornerSel) {
    if (axis == 0) return origin + vec3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + vec3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + vec3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
}

void main() {
    vVisibilityExtent = ivec3(scatterFbResolution.xyz);
    const ivec2 canvasSize = textureSize(triangleDistances, 0);
    ivec2 ij;
    vec4 color;
    int rawDist;
    if (overflowMode != 0) {
        // This appended cardinal loser carries the exact (cardinal cell,
        // encoded distance) pair the store would have written, plus its packed
        // color. The rest of the vertex path is
        // bit-identical to the cell path; only the data source differs.
        const uint entryBase = uint(gl_InstanceID) * 3u;
        const uint packedCell = compactedCells[entryBase + 0u];
        ij = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
        color = unpackColor(compactedCells[entryBase + 1u]);
        color.a = 1.0;
        rawDist = int(compactedCells[entryBase + 2u]);
    } else {
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
        vFaceOrigin = vec3(0.0);
        vFaceId = 0;
        vOwnerPixel = ivec2(-1);
        vDepth = 1.0;
        vIsoDepth = 0.0;    // unused (discarded in fragment)
        vDepthColorMode = 0;
        vDepthColorExtent = 0.0;
        vQuadParam = vec2(0.5);
        vCellTieOffset = 0.0;
        return;
    }
    const int slot = decodeSlot(rawDist);
    const int vFrac4 = decodeVFrac4PerAxis(rawDist);
    const int uFrac4 = decodeUFrac4PerAxis(rawDist);
    const int wFrac4 = decodeWFrac4PerAxis(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist); // pos3DtoDistance of the face origin (world units)
    // A flipped cell is the opposite-polarity face of its slot's axis.
    // The stored plane origin already sits on the flipped plane (the store
    // bakes polarity via faceMicroPositionFixed6 and (pixel, depth) inverts
    // exactly), and the two polarities share their in-plane span axes — so
    // origin recovery is polarity-independent; only faceId itself flips
    // (slot-key + the debug overlays stay exact).
    const int faceId = visibleFaceIds[slot] ^ flip;
    const int axis = faceId >> 1;

    // Recover the exact face origin from the un-yawed (cardinal) iso store. The
    // store (c_voxel_to_trixel_stage_1.glsl) files this face at
    // `perAxisBase + pos3DtoPos2DIso(facePos)`, so the cardinal iso pixel is
    // `ij - perAxisBase` and isoPixelToPos3D inverts it exactly against rawDepth
    // (= x+y+z of the face plane). Non-singular at every yaw because the recovered
    // index is UN-yawed; the live yaw is applied only at projection.
    vec3 eu, ev;
    faceInPlaneUnitAxes(axis, eu, ev);
    const ivec2 isoPix = ij - perAxisBase;
    const vec3 baseOrigin = isoPixelToPos3D(isoPix.x, isoPix.y, float(rawDepth));
    // Apply the sub-cell offsets packed in the encoding: u/v shift
    // within the face plane; w moves the plane itself along the face axis —
    // without it every fractionally-positioned face snaps to the integer
    // lattice plane and the entity's faces stop meeting at shared edges.
    // Mirror of perAxisSubCellFrac (ir_per_axis_lighting.glsl) — this scatter
    // is the PRODUCER that DEFINES the `/16 - 0.5` centring convention the
    // lighting consumers decode; kept inline (not routed through the helper)
    // because pulling ir_per_axis_lighting into this TU is exactly the
    // FP-scheduling perturbation that fragment is separated to avoid.
    const vec3 origin = baseOrigin
        + eu * (float(uFrac4) / 16.0 - 0.5)
        + ev * (float(vFrac4) / 16.0 - 0.5)
        + faceOutOfPlaneUnitAxis(axis) * (float(wFrac4) / 16.0 - 0.5);

    // Project the selected face corner under the continuous yaw (the yawed
    // projection is linear, so this IS P(theta)*corner — the true deformed
    // footprint, with no gather / parity inverse). The recovered origin is
    // lower-corner lattice, so the cell-anchored form rotates the face about the
    // authored position instead of orbiting it by the half cell.
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    // Screen re-projection anchor. `perAxisBase` is the STORE anchor —
    // trixelOriginOffsetZ1(canvasSize) (== canvasSize/2 - (1,1)) + floor(cameraIso).
    // Its (-1,-1) is the trixel grid's sub-pixel LATTICE alignment: a
    // canvas-STORAGE convention the `ij - perAxisBase` recovery depends on,
    // but NOT a screen offset. The forward scatter emits true face quads (no
    // trixel-grid gather), so that lattice alignment must not ride into the
    // on-screen placement — anchor the re-projection on the canvas geometric
    // CENTER (canvasSize/2), which the model matrix maps to screen center. The
    // shift back from the storage origin to the center is exactly +(1,1)
    // (canvasSize/2 - trixelOriginOffsetZ1(canvasSize)). The cardinal gather's
    // on-screen focus carries no such offset; anchoring on perAxisBase instead
    // registers the scatter a constant ~1 iso px (per axis, zoom-scaled) off the
    // cardinal frames at every non-cardinal yaw.
    const ivec2 reprojBase = perAxisBase + ivec2(1);
    const vec2 cornerIso =
        vec2(reprojBase) + pos3DtoPos2DIsoYawedCellAnchor(worldCorner, visualYaw);

    // Inverse of the gather's aPos->canvasPixel map (v_trixel_to_framebuffer):
    //   canvasPixel = (aPos.x + 0.5, -aPos.y + 0.5) * canvasSize
    // so the scatter lands at the same screen scale/offset as the fast path.
    vec2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5;
    quadPos.y = 0.5 - cornerIso.y / float(canvasSize.y);
    gl_Position = mpMatrix * vec4(quadPos, 1.0, 1.0);

    vColor = color;
    vFaceOrigin = origin;
    vFaceId = faceId;
    vOwnerPixel = overflowMode != 0 ? ivec2(-1) : ij;
    // Cell-anchor sum — keeps the depth-color binning consistent with the
    // authored-lattice depth the composite key carries.
    vIsoDepth = origin.x + origin.y + origin.z - 1.5;
    vDepthColorMode = depthColorMode;
    vDepthColorExtent = depthColorExtent;
    if (scatterDebugMode == kOverlayPerAxisId) {
        vColor = vec4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
    } else if (scatterDebugMode == kOverlayPerAxisMargin) {
        // No margin is drawn; the fragment stage uses the exact-face tint.
        vColor = vec4(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0, 1.0);
        vDepthColorMode = -1;
    } else if (scatterDebugMode == kOverlayPerAxisOrigin) {
        // Cell-parity brightness modulation: distinguishes WHICH cell's quad
        // covers a pixel (adjacent cells alternate brightness) on top of the
        // recovered-depth hue.
        float cellParity = float((ij.x + ij.y) & 1) * 0.45 + 0.55;
        vColor = vec4(hueWheel(float(rawDepth) / kOriginHuePeriod) * cellParity, 1.0);
    }

    vQuadParam = cornerSel;

    // Yaw-consistent composite depth, per-fragment PLANAR + exact. The stored
    // `rawDepth` (= un-yawed world x+y+z) is the face-local origin-recovery KEY
    // and must not change. Each corner emits the continuous yawed camera-space
    // depth of its finite corner point — yawedIsoDistanceCellAnchor, the
    // shared composite depth metric in ir_iso_common.glsl, so it co-sorts with
    // the SDF (c_shapes_to_trixel smoothYaw). Linear interpolation then
    // reproduces the face plane's affine depth field at every fragment.
    // Subdivided composite-depth scale. The SDF floor + cardinal voxel gather
    // encode iso-depth SUBDIVIDED (worldDepth × effSub × 8); the per-axis store
    // is BASE-resolution, so its recovered worldCorner is in world units. Lift it
    // to the same subdivided magnitude (effSub, carried in
    // effectiveSubdivisionsForHover.x) so SDF + scattered voxels co-sort at every
    // zoom — otherwise the floor out-scales the voxels ~effSub× at high zoom and
    // clips them into the floor. Scale only the iso-depth (×kDepthEncodeShift)
    // term, NOT the slot tiebreak, so slot stays a unit-scale tiebreak comparable
    // to the SDF's face bits. worldCorner carries the sub-cell offset, so this
    // scale-up keeps sub-cell depth precision (no z-fight).
    const float subScale = max(effectiveSubdivisionsForHover.x, 1.0);
    const float encScale = float(kDepthEncodeShift) * subScale;
    // Tiebreak mirrors the integer encode's low bits ((flip << 2) | slot) so a
    // flipped cell co-sorts exactly where a real cardinal store would land it.
    // Cell-anchor depth: measured at the corner's authored-lattice world point so
    // voxel and SDF surfaces at one world location co-sort exactly at every
    // residual.
    const float cornerKey = yawedIsoDistanceCellAnchor(worldCorner, visualYaw) * encScale +
                            float((flip << 2) | slot);
    const float depthRange = float(kMaxTriangleDistance - kMinTriangleDistance);
    vDepth = (cornerKey + float(distanceOffset - kMinTriangleDistance)) / depthRange;
    // Overflow entries sit two tie bands BEHIND everything else,
    // so an entry can never beat an
    // equal-yawed-depth cell-path face (near the 120°/240° coset-depth
    // degeneracy every coset member ties in view depth — without the bias
    // the sub-band tie arbitration hands ~half the surface's pixels to
    // overflow entries as a stipple). The entries' job is
    // filling pixels NO cell quad claims (the revealed slivers are
    // background there, far beyond any bias), and any genuinely farther
    // surface is >= one voxel depth step away (~500 bands), so the two-band
    // yield changes nothing else.
    if (overflowMode != 0) {
        vDepth += 2.0 * kScatterCellTieBand;
    }
    // cell2 separates immediate lattice neighbors; displaced cells can collide:
    // in-plane world steps project to iso-diagonal or (0,+/-2) only.
    const int rank2 = (flip != 0) ? 3 : slot;
    const int cell2 = (ij.x & 1) | (ij.y & 2);
    vCellTieOffset = float((rank2 << 2) | cell2) * kScatterCellTieStep;
}
