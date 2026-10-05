#include "ir_projected_face.glsl"
// Axis-only face indices (X / Y / Z axis, polarity-blind). The 3-face
// raster helpers (`faceOffset_2x3`, `faceMicroPositionFixed`,
// `faceDeformationMatrix`) take these: the deformation matrix depends only on
// the axis, and a diamond slot is a workgroup label, not a polarity.
const int kXFace = 0;
const int kYFace = 1;
const int kZFace = 2;

// Polarity-aware six-face IDs — see `docs/design/voxel-face-rasterization.md`
// and the matching `IRMath::FaceId` enum in `engine/math/include/irreden/
// ir_math.hpp`. The per-slot visible-triplet handshake carries these via
// `FrameDataVoxelToCanvas::visibleFaceIds_`: the CPU resolves which three
// WORLD faces are camera-visible this frame and uploads their FaceId per
// visible-triplet slot; the shader uses the FaceId to gate on the
// exposed-face bit and to pick the six-face outward normal /
// micro-position. Bit positions line up with the occlusion bits in
// `IRComponents::VoxelFlags::kFaceOccluded*`:
//   bit(faceId) = 2 + faceId
const int kFaceXNeg = 0;
const int kFaceXPos = 1;
const int kFaceYNeg = 2;
const int kFaceYPos = 3;
const int kFaceZNeg = 4;
const int kFaceZPos = 5;

ivec2 pos3DtoPos2DIso(ivec3 position) {
    return ivec2(
        -position.x + position.y,
        -position.x - position.y + 2 * position.z
    );
}

float pos3DtoDistance(vec3 position) {
    return position.x + position.y + position.z;
}

int pos3DtoDistance(ivec3 position) {
    return position.x + position.y + position.z;
}

// Reconstruct 3D position from 2D iso coordinates and depth.
// The isometric depth axis (1,1,1) is perpendicular to the screen:
//   pos3DtoPos2DIso(p + d*(1,1,1)) == pos3DtoPos2DIso(p) for any d.
// Given (isoX, isoY) and depth d = x+y+z, (x,y,z) is uniquely determined.
vec3 isoPositionToPos3D(vec2 iso, float depth) {
    const float x = (2.0 * depth - 3.0 * iso.x - iso.y) / 6.0;
    return vec3(x, x + iso.x, (iso.y + 2.0 * x + iso.x) / 2.0);
}

vec3 isoPixelToPos3D(int isoX, int isoY, float depth) {
    return isoPositionToPos3D(vec2(isoX, isoY), depth);
}

vec3 isoToLocal3D(ivec2 isoRel, float depth) {
    return isoPixelToPos3D(isoRel.x, isoRel.y, depth);
}

vec4 unpackColor(uint packedColor) {
    return vec4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

// Exact inverse of unpackColor: clamp to [0,1] and round-to-nearest so a
// round-trip of a stored 8-bit channel is a fixed point (the overflow-face
// relight rewrites an entry's colorPacked in place through this).
uint packColor(vec4 c) {
    uvec4 q = uvec4(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
    return q.r | (q.g << 8) | (q.b << 16) | (q.a << 24);
}

// PCG-flavored integer hash (low-collision, no FP precision loss). Cheap enough
// for per-thread shader use; quality is sufficient for visual jitter on the
// stateless particle path and any other "I need a deterministic
// pseudo-random scalar from (i, j, k)" producer.
uint hash3(uint a, uint b, uint c) {
    uint h = a * 0x9E3779B1u;
    h = (h ^ b) * 0x85EBCA77u;
    h = (h ^ c) * 0xC2B2AE3Du;
    h ^= h >> 16;
    h *= 0x85EBCA77u;
    h ^= h >> 13;
    h *= 0xC2B2AE3Du;
    h ^= h >> 16;
    return h;
}

// Map a uint seed to a unit-cube random vector in [-1, 1]^3. Three independent
// PCG outputs derived from rotated seeds keep components independent.
vec3 randomUnitVec(uint seed) {
    const float kInvU32 = 1.0 / 4294967295.0;
    uint rx = hash3(seed, 0x9E3779B1u, 0u);
    uint ry = hash3(seed, 0x85EBCA77u, 1u);
    uint rz = hash3(seed, 0xC2B2AE3Du, 2u);
    return vec3(
        float(rx) * kInvU32 * 2.0 - 1.0,
        float(ry) * kInvU32 * 2.0 - 1.0,
        float(rz) * kInvU32 * 2.0 - 1.0
    );
}

// Map local invocation ID within a (2, 3, 1) workgroup to a face type.
// (0,0),(1,0) -> Z_FACE; (1,1),(1,2) -> X_FACE; (0,1),(0,2) -> Y_FACE
//
// Takes the .xy of `gl_LocalInvocationID` as a parameter rather than reading
// the built-in directly so this helper compiles inside vertex/fragment
// shaders that include this header (e.g. `f_trixel_to_framebuffer.glsl`).
// Strict GLSL frontends (Mesa) error on `gl_LocalInvocationID` references
// even from unused functions outside compute stages. Mirrors the Metal
// counterpart in `ir_iso_common.metal`.
int localIDToFace_2x3(uvec2 localId) {
    if (localId.y == 0) return kZFace;
    if (localId.x == 1) return kXFace;
    return kYFace;
}

// Face offset within the 2x3 trixel diamond for a given face and sub-pixel
// index (0 or 1).  Matches the layout used by localIDToFace_2x3():
//   Z -> (0,0),(1,0)   X -> (1,1),(1,2)   Y -> (0,1),(0,2)
ivec2 faceOffset_2x3(int face, int subPixel) {
    if (face == kZFace) return ivec2(subPixel, 0);
    if (face == kXFace) return ivec2(1, 1 + subPixel);
    return ivec2(0, 1 + subPixel);
}

// Single-canvas distance-encoding scale: one depth unit spans 8 codes —
// [31:3] depth | [2] flip | [1:0] slot. Mirrors IRRender::kDepthEncodeShift
// (ir_render_types.hpp) and the .metal twin; every composite writer that
// lifts a world/model iso depth into shared framebuffer depth-key units
// multiplies by this.
const int kDepthEncodeShift = 8;

// Encode depth with face priority for deterministic depth-test resolution.
// The *8 spacing keeps flip + slot below every depth boundary, so raw-int
// atomicMin still orders by depth first (a flipped-but-farther cell can
// never win the min) and the Hi-Z max stays a faithful max-depth pyramid.
// `flip` marks a silhouette-riser face emitted with the OPPOSITE polarity of
// its slot's triplet face (faceId = visibleFaceIds[slot] ^ 1): lighting/AO/
// shadow decode it to negate the slot-derived outward normal instead of
// shading the riser with an inverted Lambert.
int encodeDepthWithFace(int rawDepth, int face, int flip) {
    return rawDepth * kDepthEncodeShift + (flip << 2) + face;
}

// Unflipped overload — the common case (SDF shapes, particles, resolve
// re-emits of unflipped cells, non-riser voxel faces).
int encodeDepthWithFace(int rawDepth, int face) {
    return encodeDepthWithFace(rawDepth, face, 0);
}

// Shared decode helpers — the ONLY places the two distance-encoding bit
// layouts live. Single-canvas: [31:3] depth | [2] flip | [1:0] slot.
// Per-axis: [31:15] depth | [14:11] wFrac4 | [10] flip | [9:6] uFrac4 |
// [5:2] vFrac4 | [1:0] slot. Depth decodes by arithmetic right shift (floor),
// so negative depths recover exactly; slot/flip/fracs are pure low-bit masks.
// wFrac sits directly below depth so atomicMin still orders by true plane
// depth (a same-cell nearer plane wins) before flip/frac/slot. Route every
// consumer through these — an open-coded shift silently mis-decodes when a
// layout changes.
int decodeSlot(int encoded) { return encoded & 3; }
int decodeFlipSingle(int encoded) { return (encoded >> 2) & 1; }
int decodeDepthSingle(int encoded) { return encoded >> 3; }
int decodeFlipPerAxis(int encoded) { return (encoded >> 10) & 1; }
int decodeDepthPerAxis(int encoded) { return encoded >> 15; }
// 4-bit sub-cell fracs (0..15, 8 = cell centre): u/v span the face's two
// in-plane axes; w is the OUT-OF-PLANE fraction along the face axis — the
// coordinate the integer cell lattice cannot carry. Dropping w reconstructs
// every face of fractionally-positioned content on the integer lattice
// plane, displacing it along its own normal by up to half a voxel.
int decodeUFrac4PerAxis(int encoded) { return (encoded >> 6) & 15; }
int decodeVFrac4PerAxis(int encoded) { return (encoded >> 2) & 15; }
int decodeWFrac4PerAxis(int encoded) { return (encoded >> 11) & 15; }
// Route-aware forms for the shared lighting/AO/shadow/bake consumers that
// read either encoding behind the perAxisRoute selector.
int decodeDepthRoute(int encoded, int perAxisRoute) {
    return perAxisRoute != 0 ? decodeDepthPerAxis(encoded) : decodeDepthSingle(encoded);
}
int decodeFlipRoute(int encoded, int perAxisRoute) {
    return perAxisRoute != 0 ? decodeFlipPerAxis(encoded) : decodeFlipSingle(encoded);
}

// Two-tier composite depth partition. The most-negative
// kDepthForegroundBandWidth codes of [kMinTriangleDistance, kMaxTriangleDistance]
// are reserved for foreground-priority detached solids: the framebuffer gather
// (f_trixel_to_framebuffer) clamps WORLD content out of the band and pins
// FOREGROUND content into it, so a priority solid is unconditionally nearer than
// any world fragment regardless of world extent. The far edge is
// foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth. Mirrors
// IRRender::kDepthForegroundBandWidth (ir_render_types.hpp) and the .metal twin.
const int kDepthForegroundBandWidth = 16384;

// Per-trixel priority tiers. Subdivide the reserved foreground band into
// N-1 disjoint equal-width tiers; tier 0 = world (out of band). MORE-negative =
// higher priority, so tier N-1 sits at the near (most-negative) band edge.
// f_trixel_to_framebuffer selects `tier = max(perEntityTier, perTrixelTier)` per
// fragment, then pins enc into depthForegroundTier{Lo,Hi}, centered on
// depthForegroundTierCenter. Mirror IRRender::kDepthForegroundTier*
// (ir_render_types.hpp) + the .metal twin.
const int kDepthForegroundTierCount = 3;
const int kDepthForegroundTierWidth = kDepthForegroundBandWidth / (kDepthForegroundTierCount - 1);
int depthForegroundTierLo(int kMin, int tier) {
    return kMin + (kDepthForegroundTierCount - 1 - tier) * kDepthForegroundTierWidth;
}
int depthForegroundTierHi(int kMin, int tier) {
    return depthForegroundTierLo(kMin, tier) + kDepthForegroundTierWidth - 1;
}
int depthForegroundTierCenter(int kMin, int tier) {
    return depthForegroundTierLo(kMin, tier) + kDepthForegroundTierWidth / 2;
}

// Per-trixel priority carrier. The per-trixel tier rides the top K=2 bits
// of the 64-bit entity id stored in the triangleEntityIds channel (uvec2: .x =
// low word, .y = high word; the carrier is bits 30..31 of the high word). THE
// chokepoint: every reader masks via decodeEntityId, the stage-2 writer packs via
// encodeEntityIdWithPriority — no site open-codes the mask. Priority 0 ⇒ id
// unchanged. Mirror IRRender::kEntityIdPriority* (ir_render_types.hpp) + .metal.
const uint kEntityIdPriorityShiftInHighWord = 30u;
const uint kEntityIdPriorityMaskInHighWord = 0x3u << kEntityIdPriorityShiftInHighWord;
// Fog cut-face carrier: the bit just below the priority tier (bit 29 of the
// high word) flags a fog cross-section CUT face so LIGHTING_TO_TRIXEL forces it
// fully lit — no self-shadow from the fog-hidden neighbor voxels, no
// interior-crease AO. Rides the SAME masking chokepoint as the priority tier:
// kEntityIdHighWordMask strips it, so every id READER (picking) ignores it. A
// non-cut face leaves the stored id unchanged.
const uint kEntityIdCutFaceMaskInHighWord = 0x1u << 29u;
// Fog BODY carrier: bit 28 of the high word flags a pixel of a BODY-classed
// subject (voxel reserved bit 3, or the shape flag
// SHAPE_FLAG_FOG_WHOLE_BODY_EXEMPT) and bits 27:20 carry its 8-bit reveal
// factor, so FOG_TO_TRIXEL paints the pixel at factor / 255 with no field
// lookup. Entity ids are allocation counters, so a live id never sets bits
// 27:20. Same masking chokepoint as the bits above.
const uint kEntityIdFogBodyMaskInHighWord = 0x1u << 28u;
const uint kEntityIdFogWholeBodyMaskInHighWord = kEntityIdFogBodyMaskInHighWord;
const uint kEntityIdFogBodyFactorShiftInHighWord = 20u;
const uint kEntityIdFogBodyFactorMaskInHighWord = 0xFFu << kEntityIdFogBodyFactorShiftInHighWord;
// Analytic-surface carrier: bit 19 of the high word flags a pixel the shape
// raster wrote — an exact world surface point — as opposed to a voxel raster
// point on the lower-corner cell lattice. FOG_TO_TRIXEL's line-of-sight gate
// maps the two onto one occluder lattice. Entity ids occupy the low word plus
// the flag bits 0..2 of the high word, so bit 19 never carries id state. Same
// masking chokepoint as the bits above.
const uint kEntityIdAnalyticSurfaceMaskInHighWord = 0x1u << 19u;
const uint kEntityIdHighWordMask =
    ~(kEntityIdPriorityMaskInHighWord | kEntityIdCutFaceMaskInHighWord |
      kEntityIdFogBodyMaskInHighWord | kEntityIdFogBodyFactorMaskInHighWord |
      kEntityIdAnalyticSurfaceMaskInHighWord);
uint decodePriority(uvec2 rawId) {
    return (rawId.y >> kEntityIdPriorityShiftInHighWord) & 0x3u;
}
bool decodeCutFace(uvec2 rawId) {
    return (rawId.y & kEntityIdCutFaceMaskInHighWord) != 0u;
}
bool decodeFogBody(uvec2 rawId) {
    return (rawId.y & kEntityIdFogBodyMaskInHighWord) != 0u;
}
bool decodeFogWholeBody(uvec2 rawId) {
    return decodeFogBody(rawId);
}
// The BODY reveal factor in 0..255; meaningful only when decodeFogBody.
uint decodeFogBodyFactor(uvec2 rawId) {
    return (rawId.y >> kEntityIdFogBodyFactorShiftInHighWord) & 0xFFu;
}
bool decodeAnalyticSurface(uvec2 rawId) {
    return (rawId.y & kEntityIdAnalyticSurfaceMaskInHighWord) != 0u;
}
uvec2 decodeEntityId(uvec2 rawId) {
    return uvec2(rawId.x, rawId.y & kEntityIdHighWordMask);
}
uvec2 encodeEntityIdWithPriority(uvec2 id, uint priority) {
    return uvec2(id.x, (id.y & kEntityIdHighWordMask) |
                           ((priority & 0x3u) << kEntityIdPriorityShiftInHighWord));
}
// Set the fog cut-face flag on an ALREADY priority-encoded id. Call after
// encodeEntityIdWithPriority (which strips this bit via kEntityIdHighWordMask).
// `packedId`, not `packed` — `packed` is a reserved GLSL keyword (the
// layout qualifier); NVIDIA's compiler rejects it as an identifier while
// the GLSL->Metal path accepts it, so the slip only surfaces on GL hosts.
uvec2 encodeEntityIdCutFace(uvec2 packedId, bool isCutFace) {
    return isCutFace ? uvec2(packedId.x, packedId.y | kEntityIdCutFaceMaskInHighWord)
                     : packedId;
}
// Fold the fog BODY class bit and its 8-bit reveal factor into an ALREADY
// priority-encoded id, like encodeEntityIdCutFace. A non-BODY leaves the id
// unchanged.
uvec2 encodeEntityIdFogBody(uvec2 packedId, bool isFogBody, uint factor) {
    return isFogBody
        ? uvec2(packedId.x, packedId.y | kEntityIdFogBodyMaskInHighWord |
                                ((factor & 0xFFu) << kEntityIdFogBodyFactorShiftInHighWord))
        : packedId;
}
// Set the analytic-surface flag on an ALREADY priority-encoded id, like
// encodeEntityIdCutFace.
uvec2 encodeEntityIdAnalyticSurface(uvec2 packedId) {
    return uvec2(packedId.x, packedId.y | kEntityIdAnalyticSurfaceMaskInHighWord);
}
// A subject whose raster route carries no factor of its own (a flagged SDF
// shape) renders whole: the class bit with the factor pinned at 255.
uvec2 encodeEntityIdFogWholeBody(uvec2 packedId, bool isFogWholeBody) {
    return encodeEntityIdFogBody(packedId, isFogWholeBody, 255u);
}
// A per-axis overflow entry carries its fog class in its colour's alpha byte
// (the scatter forces the entry opaque): 255 is FIELD, and a BODY's 8-bit
// factor is rescaled onto 0..254 so a fully revealed body still reads state
// 1.0 (ir_fog_common fogOverflowBodyState).
const uint kFogOverflowFieldByte = 255u;
uint encodeFogOverflowClassByte(bool isFogBody, uint factor) {
    return isFogBody ? ((factor & 0xFFu) * 254u + 127u) / 255u : kFogOverflowFieldByte;
}

// Per-axis fractional encoding:
// (depth << 15) | (wFrac4 << 11) | (flip << 10) | (uFrac4 << 6)
// | (vFrac4 << 2) | slot. Frac fields in 0..15 where 8 = cell centre
// (fracInCell = 0): u/v are the face's in-plane sub-cell offsets, w the
// out-of-plane offset along the face axis. atomicMin orders by depth first,
// then wFrac (the true-plane-depth remainder — a same-cell nearer plane
// wins), then flip — the same relative invariant as the single-canvas
// encode. Per-axis canvases clear to INT_MAX (0x7FFFFFFF) so any valid
// encoding overwrites the sentinel. rawDepth must be in world units; the
// depth field is 17 bits so rawDepth must stay < 2^16.
int encodeDepthWithFaceFrac(
    int rawDepth, int slot, int uFrac4, int vFrac4, int wFrac4, int flip
) {
    return (rawDepth << 15) | (wFrac4 << 11) | (flip << 10) | (uFrac4 << 6) |
           (vFrac4 << 2) | slot;
}

// Maps fracInCell to the three 4-bit sub-cell offsets (0..15, 8 = cell
// centre) for the given axis: u/v follow the uv assignment of
// faceInPlaneUnitAxes; w is the fracInCell component along the face axis.
void fracToFrac4(int axis, vec3 fracInCell, out int uFrac4, out int vFrac4, out int wFrac4) {
    if (axis == 0) {
        uFrac4 = clamp(int(fracInCell.y * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.z * 16.0) + 8, 0, 15);
        wFrac4 = clamp(int(fracInCell.x * 16.0) + 8, 0, 15);
    } else if (axis == 1) {
        uFrac4 = clamp(int(fracInCell.x * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.z * 16.0) + 8, 0, 15);
        wFrac4 = clamp(int(fracInCell.y * 16.0) + 8, 0, 15);
    } else {
        uFrac4 = clamp(int(fracInCell.x * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.y * 16.0) + 8, 0, 15);
        wFrac4 = clamp(int(fracInCell.z * 16.0) + 8, 0, 15);
    }
}

// Convenience overload: compute all three fracs from fracInCell and encode
// in one call.
int encodeDepthWithFaceFrac(int rawDepth, int slot, int axis, vec3 fracInCell, int flip) {
    int uFrac4, vFrac4, wFrac4;
    fracToFrac4(axis, fracInCell, uFrac4, vFrac4, wFrac4);
    return encodeDepthWithFaceFrac(rawDepth, slot, uFrac4, vFrac4, wFrac4, flip);
}

// Unit vector of a face's out-of-plane axis — the direction the wFrac
// offset moves the reconstructed plane. Companion to faceInPlaneUnitAxes.
vec3 faceOutOfPlaneUnitAxis(int axis) {
    return vec3(axis == 0 ? 1.0 : 0.0, axis == 1 ? 1.0 : 0.0, axis == 2 ? 1.0 : 0.0);
}

// Outward unit normal for the visible side of each iso-rendered face. The
// iso projection has view direction (1,1,1), so at cardinal 0 the three
// faces a camera at (-large, -large, -large) sees are the ones whose
// outward normals point AGAINST the view direction — i.e. world -X, -Y,
// -Z (+Z is down, so -Z is up = the top face). Used by both AO compute
// and lighting lambert; both consumers MUST share this so AO sampling
// and shading agree on which way is "out".
//
// At non-zero cardinal the camera-visible faces rotate; AO and lighting
// must call `faceOutwardNormal6` with the per-slot `visibleFaceIds[slot]`
// from the UBO instead of the slot itself. This 3-face overload is for
// callers that genuinely want the axis-only X_NEG/Y_NEG/Z_NEG normals
// (e.g. the SDF shape rasterizer at cardinal 0).
vec3 faceOutwardNormal(int face) {
    if (face == kXFace) return vec3(-1.0, 0.0, 0.0);
    if (face == kYFace) return vec3(0.0, -1.0, 0.0);
    return vec3(0.0, 0.0, -1.0);
}

ivec3 faceOutwardNormalI(int face) {
    if (face == kXFace) return ivec3(-1, 0, 0);
    if (face == kYFace) return ivec3(0, -1, 0);
    return ivec3(0, 0, -1);
}

// Six-face polarity-aware outward unit normal. `faceId` must be one of
// `kFaceXNeg`/.../`kFaceZPos` (0..5) — typically read from
// `visibleFaceIds[slot]` in the per-frame UBO. CPU mirror:
// `IRMath::faceOutwardNormal(FaceId)`.
vec3 faceOutwardNormal6(int faceId) {
    if (faceId == kFaceXNeg) return vec3(-1.0, 0.0, 0.0);
    if (faceId == kFaceXPos) return vec3( 1.0, 0.0, 0.0);
    if (faceId == kFaceYNeg) return vec3(0.0, -1.0, 0.0);
    if (faceId == kFaceYPos) return vec3(0.0,  1.0, 0.0);
    if (faceId == kFaceZNeg) return vec3(0.0, 0.0, -1.0);
    return vec3(0.0, 0.0, 1.0);  // kFaceZPos
}

// Integer outward normal — same six-face semantics as `faceOutwardNormal6`,
// suitable for AO neighbor-step arithmetic that wants the world-frame
// ±1 vector without float round-trip.
ivec3 faceOutwardNormal6I(int faceId) {
    if (faceId == kFaceXNeg) return ivec3(-1, 0, 0);
    if (faceId == kFaceXPos) return ivec3( 1, 0, 0);
    if (faceId == kFaceYNeg) return ivec3(0, -1, 0);
    if (faceId == kFaceYPos) return ivec3(0,  1, 0);
    if (faceId == kFaceZNeg) return ivec3(0, 0, -1);
    return ivec3(0, 0, 1);  // kFaceZPos
}

// Returns true when @p faceId is exposed (neighbor cell empty/absent)
// according to the per-voxel flags byte. The encoding mirrors
// `IRComponents::VoxelFlags::kFaceOccluded*`: bit `(2 + faceId)` is set
// when the matching neighbor is active, so the face should NOT emit. Per the
// exposed-face gate in docs/design/voxel-face-rasterization.md
// (`emit ⟺ visible ∧ exposed`).
bool faceIsExposed(uint flagsByte, int faceId) {
    return ((flagsByte >> uint(2 + faceId)) & 1u) == 0u;
}

ivec3 faceMicroPositionFixed(int face, ivec3 voxelPositionFixed, int u, int v, int subdivisions) {
    if (face == kXFace) {
        return ivec3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (face == kYFace) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    return ivec3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z
    );
}

// Six-face polarity-aware micro position. For POS faces the fixed-axis
// coordinate sits at `voxelPositionFixed.<axis> + subdivisions` (the
// high-coordinate side of the voxel); for NEG faces it sits at
// `voxelPositionFixed.<axis>` (the low-coordinate side, identical to
// the 3-face `faceMicroPositionFixed`). The other two axes sweep
// `u, v ∈ [0, subdivisions)` exactly as the 3-face overload does.
ivec3 faceMicroPositionFixed6(
    int faceId,
    ivec3 voxelPositionFixed,
    int u,
    int v,
    int subdivisions
) {
    if (faceId == kFaceXNeg) {
        return ivec3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceXPos) {
        return ivec3(
            voxelPositionFixed.x + subdivisions,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceYNeg) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceYPos) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y + subdivisions,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceZNeg) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y + v,
            voxelPositionFixed.z
        );
    }
    // kFaceZPos
    return ivec3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z + subdivisions
    );
}

bool isInsideCanvas(ivec2 pixel, ivec2 canvasSize) {
    return pixel.x >= 0 && pixel.x < canvasSize.x &&
           pixel.y >= 0 && pixel.y < canvasSize.y;
}

// Shadow-feeder classification: on the cardinal single-canvas world route, a
// voxel whose cardinal iso position lies outside the UN-widened visible
// viewport but inside the shadow-feeder-widened cull exists only to cast sun
// shadows onto on-screen pixels through stage 1's distance bake — it is never
// displayed, lit, or picked.
//
// Two kernels ask this same question and must agree: c_voxel_visibility_compact
// partitions survivors into the visible list vs the strided off-screen feeder
// list, and c_voxel_to_trixel_stage_2 skips a feeder's colour + entity-id taps.
// A voxel the compact calls VISIBLE while stage 2 skips it drops an on-screen
// pixel's colour, so over-classifying VISIBLE is the only safe failure
// direction.
//
// The route terms are part of the predicate, not a caller-side gate: the
// widened cull only exists on the cardinal (residualYaw == 0) world
// (isDetachedCanvas < 0.5) route, so both terms must hold before an
// out-of-bounds iso means "feeder". When sun shadows are off,
// visibleIsoBounds == cullIsoMin/Max and this never fires.
bool isShadowFeederIso(
    ivec2 isoPos,
    ivec4 visibleIsoBounds,
    float residualYaw,
    float isDetachedCanvas
) {
    return residualYaw == 0.0 && isDetachedCanvas < 0.5 &&
        (isoPos.x < visibleIsoBounds.x || isoPos.x > visibleIsoBounds.z ||
         isoPos.y < visibleIsoBounds.y || isoPos.y > visibleIsoBounds.w);
}

// Hardware round() is safe here: the 1e-4 near-grid gate excludes
// half-integer inputs, so the tie direction — the only point where
// round() is implementation-defined — is unobservable.
vec3 snapNearIntegerVoxelPosition(vec3 voxelPosition) {
    vec3 voxelRounded = round(voxelPosition);
    bvec3 nearGrid = lessThanEqual(abs(voxelPosition - voxelRounded), vec3(0.0001));
    return mix(voxelPosition, voxelRounded, vec3(nearGrid));
}

// Round-half-up: rounds to the nearest integer, ties go UP. Mirrors
// `IRMath::roundHalfUp` (engine/math/include/irreden/ir_math.hpp) so any
// CPU↔GPU coordinate handshake (occupancy grid build, ray-march cell sampling)
// resolves half-integer voxel positions to the same cell on both sides.
// Hardware `round()` is implementation-defined at half-integers and cannot be
// trusted for that handshake.
ivec3 roundHalfUp(vec3 v) {
    return ivec3(floor(v + vec3(0.5)));
}

// Per-axis faces encode translation in signed sixteenths, independent of
// subdivisions. Axis 2 maps the packed u/v/w fractions to world x/y/z.
vec3 perAxisRenderedVoxelCenter(vec3 voxelPosition) {
    const vec3 aligned = snapNearIntegerVoxelPosition(voxelPosition);
    const ivec3 cell = roundHalfUp(aligned);
    int u, v, w;
    fracToFrac4(2, aligned - vec3(cell), u, v, w);
    return vec3(cell) + (vec3(u, v, w) / 16.0 - vec3(0.5));
}

int roundHalfUp(float v) {
    return int(floor(v + 0.5));
}

ivec2 roundHalfUp(vec2 v) {
    return ivec2(floor(v + vec2(0.5)));
}

// Per-voxel iso occlusion depth of model position `pos` projected onto a
// (possibly entity-rotated) iso depth `axis` — the SO(3) generalization of
// pos3DtoDistance (identical to it when axis == (1,1,1)). For a rotated
// DETACHED canvas `axis` is `R⁻¹·(1,1,1)` (uploaded in
// FrameDataVoxelToTrixel.voxelDepthAxis); the world canvas keeps (1,1,1).
// CPU twin: IRMath::isoDepthAlongAxis — roundHalfUp keeps the half-integer
// rounding bit-identical across the CPU/GPU boundary.
int isoDepthAlongAxis(ivec3 pos, vec3 axis) {
    return roundHalfUp(dot(vec3(pos), axis));
}

ivec2 trixelOriginOffsetX1(ivec2 trixelCanvasSize) {
    return trixelCanvasSize / ivec2(2);
}

ivec2 trixelOriginOffsetZ1(ivec2 trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);
}

// Clamp a float canvas-pixel position into a valid `texelFetch()` index. Metal
// twin: `trixelCanvasReadCoord` in `metal/ir_iso_common.metal`. Both backends
// select the canvas texel by this explicit integer rule rather than normalized
// sampler addressing, whose edge handling follows each texture's wrap mode
// (REPEAT on the colour/distance canvases) and whose texel pick is computed at
// implementation-defined precision.
ivec2 trixelCanvasReadCoord(vec2 origin, ivec2 canvasSize) {
    return ivec2(clamp(origin, vec2(0.0), vec2(canvasSize - 1)));
}

int trixelOriginModifier(ivec2 trixelCanvasOffsetZ1, vec2 frameCanvasOffset) {
    vec2 canvasOffsetFloored = floor(frameCanvasOffset);
    return (trixelCanvasOffsetZ1.x + trixelCanvasOffsetZ1.y +
            int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
}

// Trixel-cell diagonal split. Each iso texel-cell holds two triangles split
// along a diagonal; this resolves which half a canvas position covers by
// conditionally decrementing `origin.y` one row (parity bit + a sub-pixel
// `fract` test). It only ever adjusts `.y`, and is byte-identical to CPU
// `pos2DIsoToTriangleIndex` (ir_math.cpp).
//
// The trixel->framebuffer gather does not use it in RECTANGULAR display:
// every texture read — color, depth, tier and the hover entity id — and the
// hover compare sample the raw texel (the cell it selects straddles two raw
// texel rows, so it cannot name one stored texel). LOCAL_TRIANGLES supplies
// canvas-local parity and a row-corrected query through
// localTrixelFramebufferSamplePosition; its caller rejects out-of-bounds
// results before texture reads.
vec2 trixelFramebufferSamplePosition(vec2 origin, int originModifier) {
    vec2 originFlooredComp = floor(origin);
    vec2 fractComp = fract(origin);
    if (mod(originFlooredComp.x + originFlooredComp.y + float(originModifier), 2.0) >= 1.0) {
        if (fractComp.y < fractComp.x) {
            origin.y -= 1.0;
        }
    } else if (fractComp.y < 1.0 - fractComp.x) {
        origin.y -= 1.0;
    }
    return origin;
}

// Private storage parity excludes the world placement of the canvas quad.
int localTrixelOriginParity(ivec2 originZ1) {
    return (originZ1.x + originZ1.y) & 1;
}

vec2 localTrixelCellCentroid(ivec2 cell, int originParity) {
    const bool odd = ((cell.x + cell.y + originParity) & 1) != 0;
    return vec2(cell) + vec2(odd ? 1.0 / 3.0 : 2.0 / 3.0, 0.0);
}

// A local display triangle is centered one row below its stored index.
vec2 localTrixelFramebufferSamplePosition(vec2 origin, ivec2 originZ1) {
    return trixelFramebufferSamplePosition(
        origin + vec2(0.0, 1.0), localTrixelOriginParity(originZ1));
}

int effectiveTrixelSubdivisionScale(ivec2 voxelRenderOptions) {
    return voxelRenderOptions.x != 0 ? max(voxelRenderOptions.y, 1) : 1;
}

ivec2 trixelFrameOffset(
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions
) {
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    return trixelCanvasOffsetZ1 + ivec2(floor(frameCanvasOffset * float(scale)));
}

// The per-axis store origin is the frame data's `perAxisStoreFrame.xy` — a
// WHOLE-iso cell, NOT the density-scaled `trixelFrameOffset`: per-axis canvases
// are base-resolution, so a scaled origin jitters under pan. Each per-axis
// site reads it directly rather than through a helper centralised here: adding
// a symbol to ir_iso_common perturbs the cardinal SDF/voxel shaders' FP
// scheduling and drifts their byte-identical fast path (the same reason
// perAxisCellToWorld3D lives in ir_per_axis_lighting, not here).

ivec2 trixelCanvasPixelToIsoRel(
    ivec2 pixel,
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions
) {
    return pixel - trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
}

// Cardinal Z-yaw helpers.
// FrameDataVoxelToTrixel.rasterYaw is guaranteed to be a multiple of pi/2 by
// the camera-side split helper (engine/prefabs/irreden/render/camera.hpp); the
// renderer uses one of four basis-vector permutations selected by an integer
// index in [0, 3] so integer voxel positions still land on integer trixel
// pixels post-rotation. residualYaw is absorbed by faceDeform[] in the trixel
// emit; these helpers ignore it.
//
// Sign convention: rotateCardinalZ is world->view = R_z(-rasterYaw) — same as
// the continuous-yaw matrix in c_shapes_to_trixel_body.glsl. At visualYaw=+pi/2 the
// camera turns +90 deg around +Z; from the view's POV the world appears to spin
// -90 deg, so world (+X,0,0) lands at view (0,-Y,0) and projects to iso
// (-1,+1). Voxels (this helper) and shapes MUST share this convention or they
// desync at non-zero yaw.

int rasterYawCardinalIndex(float rasterYaw) {
    // CPU snaps visualYaw to a multiple of pi/2 (Camera::computeYawSplit) so
    // this index pick is exact at floats that survived the UBO upload. The
    // round() defends against bit-wise drift only; it is not the cardinal-snap
    // policy itself. Negative inputs (yaw=-pi/2 -> q=-1) fold via the (mod 4 +
    // 4) mod 4 clamp.
    const float kHalfPi = 1.5707963267948966f;
    int q = int(round(rasterYaw / kHalfPi));
    return ((q % 4) + 4) % 4;
}

// (cos, sin) of the cardinal angle named by cardinalIndex — exact ±1/0, the
// snapped Z-yaw the GRID rasterizer projects at. Mirrors
// IRMath::cardinalYawCosSin.
vec2 cardinalYawCosSin(int cardinalIndex) {
    if (cardinalIndex == 1) return vec2( 0.0,  1.0);
    if (cardinalIndex == 2) return vec2(-1.0,  0.0);
    if (cardinalIndex == 3) return vec2( 0.0, -1.0);
    return vec2(1.0, 0.0);
}

ivec3 rotateCardinalZ(ivec3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return ivec3( v.y, -v.x, v.z);   // R_z(-pi/2)
    if (cardinalIndex == 2) return ivec3(-v.x, -v.y, v.z);   // R_z(+/-pi)
    if (cardinalIndex == 3) return ivec3(-v.y,  v.x, v.z);   // R_z(+pi/2)
    return v;
}

// View-space lower-corner displacement of the rotated unit voxel [0,1]^3:
// R_z permutes/negates axes, so the post-rotation AABB lower corner relative
// to the rotated origin is:
//   cardinal 0: (0, 0, 0)
//   cardinal 1: (0,-1, 0)  (world x in [0,1] -> view y in [-1, 0])
//   cardinal 2: (-1,-1, 0)
//   cardinal 3: (-1, 0, 0)
// The raster store, cull, and resolve chain do NOT apply this shift: they store
// the plain rotated position (the cardinal-0 raster of the rotated scene; the
// half cell projects to zero iso offset, so the mass footprint is unchanged).
// Adding the shift rotates the voxel MASS rigidly about
// `position + (0.5,0.5,0.5)` after the pivot cancel, orbiting any pinned focus
// by the half cell at cardinals 1-3, while the SDF path and the CPU picking
// math rotate about the exact position. Do not introduce it into the store or
// its inverses.
ivec3 cardinalLowerCornerShift(int cardinalIndex) {
    if (cardinalIndex == 1) return ivec3(0, -1, 0);
    if (cardinalIndex == 2) return ivec3(-1, -1, 0);
    if (cardinalIndex == 3) return ivec3(-1, 0, 0);
    return ivec3(0, 0, 0);
}

// Image of a six-face FaceId's outward normal under rotateCardinalZ (world ->
// view). Lets the subdivided cardinal raster compute face micro-positions
// NATIVELY IN VIEW SPACE — faceMicroPositionFixed6(viewFace, viewCell, ...)
// on the rotated cell — instead of rotating world-frame face planes after the
// fact. The distinction matters because a cell index is a half-open interval
// while a face PLANE is a boundary, and the two map differently under axis
// negation: a world-computed POS-face plane carried through the cell map lands
// past the neighbor faces' coverage, opening a background seam along every
// shared edge of a rotated-in POS face at cardinals 1/2/3. Z faces
// are fixed points (R_z never moves the z axis). Cardinal 3 is the inverse
// permutation of cardinal 1; cardinal 2 flips both in-plane polarities.
int rotateFaceIdCardinalZ(int faceId, int cardinalIndex) {
    if (cardinalIndex == 0 || faceId >= kFaceZNeg) return faceId;
    if (cardinalIndex == 2) return faceId ^ 1;             // +/-x -> -/+x, +/-y -> -/+y
    if (cardinalIndex == 1) {
        // world +x -> view -y, world +y -> view +x
        if (faceId == kFaceXNeg) return kFaceYPos;
        if (faceId == kFaceXPos) return kFaceYNeg;
        if (faceId == kFaceYNeg) return kFaceXNeg;
        return kFaceXPos;                                  // kFaceYPos
    }
    // cardinalIndex == 3: world +x -> view +y, world +y -> view -x
    if (faceId == kFaceXNeg) return kFaceYNeg;
    if (faceId == kFaceXPos) return kFaceYPos;
    if (faceId == kFaceYNeg) return kFaceXPos;
    return kFaceXNeg;                                      // kFaceYPos
}

vec3 rotateCardinalZInv(vec3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return vec3(-v.y,  v.x, v.z);    // R_z(+pi/2)
    if (cardinalIndex == 2) return vec3(-v.x, -v.y, v.z);    // R_z(+/-pi)
    if (cardinalIndex == 3) return vec3( v.y, -v.x, v.z);    // R_z(-pi/2)
    return v;
}

ivec3 rotateCardinalZInvI(ivec3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return ivec3(-v.y,  v.x, v.z);   // R_z(+pi/2)
    if (cardinalIndex == 2) return ivec3(-v.x, -v.y, v.z);   // R_z(+/-pi)
    if (cardinalIndex == 3) return ivec3( v.y, -v.x, v.z);   // R_z(-pi/2)
    return v;
}

vec3 isoPixelToWorld3D(int isoX, int isoY, float depth, int cardinalIndex) {
    return rotateCardinalZInv(isoPixelToPos3D(isoX, isoY, depth), cardinalIndex);
}

vec3 trixelCanvasPixelToWorld3D(
    ivec2 pixel,
    int rawDepth,
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions,
    int cardinalIndex
) {
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    ivec2 isoRel =
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (scale > 1) {
        pos3D /= float(scale);
    }
    if (cardinalIndex != 0) {
        // The rasterizer stores the plain rotated position (no lower-corner
        // shift), so the inverse is the plain cardinal rotation back to world.
        pos3D = rotateCardinalZInv(pos3D, cardinalIndex);
    }
    return pos3D;
}

vec3 trixelCanvasPixelToWorld3D(
    ivec2 pixel,
    int rawDepth,
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions,
    float rasterYaw
) {
    return trixelCanvasPixelToWorld3D(
        pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions,
        rasterYawCardinalIndex(rasterYaw)
    );
}

// View frame -> world frame under a continuous camera Z-yaw: R_z(+yaw)·v, the
// smooth companion to rotateCardinalZInv (pos3DtoPos2DIsoYawed projects the
// view point R_z(-yaw)·world, so this is its rotation inverse).
vec3 rotateYawZInv(vec3 v, float yaw) {
    float c = cos(yaw);
    float s = sin(yaw);
    return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

// Smooth-camera-yaw inverse of the smooth-yaw SDF store: those pixels are
// placed at roundHalfUp(pos3DtoPos2DIsoYawed(world, visualYaw)) with the
// VIEW-frame iso depth, so recover the view-frame point with the
// cardinal-frame solver and rotate back by the full +visualYaw. No
// lower-corner shift — the smooth store never applies one. Identical to
// trixelCanvasPixelToWorld3D at visualYaw == 0 (cos=1/sin=0, cardinal 0 takes
// the same shift-free path).
vec3 trixelCanvasPixelToWorld3DSmoothYaw(
    ivec2 pixel,
    int rawDepth,
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions,
    float visualYaw
) {
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    ivec2 isoRel =
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    vec3 viewPos = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (scale > 1) {
        viewPos /= float(scale);
    }
    return rotateYawZInv(viewPos, visualYaw);
}

// Continuous-yaw + per-face deformation math. Mirrors
// IRMath::pos3DtoPos2DIsoYawed / faceDeformationMatrix /
// deformedTrixelIsoPixel / sqtToMat4 / matrixApplyToVoxelGrid in
// engine/math/include/irreden/ir_math.hpp; CPU and GPU MUST agree at all 4
// cardinal yaws and across the [-pi/4, pi/4] residual range.

// Iso projection of a world point under a continuous Z-yaw camera.
// Equivalent to pos3DtoPos2DIso(R_z(-yaw) * world). Sign convention matches
// rotateCardinalZ (world->view = R_z(-yaw)) so this is the smooth extension
// of the cardinal-snap projection used by the voxel rasterizer.
vec2 pos3DtoPos2DIsoYawed(vec3 worldPos, float visualYaw) {
    float c = cos(visualYaw);
    float s = sin(visualYaw);
    float vx = worldPos.x * c + worldPos.y * s;
    float vy = -worldPos.x * s + worldPos.y * c;
    return vec2(-vx + vy, -vx - vy + 2.0 * worldPos.z);
}

// Rotation anchor for VOXEL-RASTER cell positions. The raster parameterizes
// the voxel authored at position p by its cell's LOWER-CORNER lattice (mass
// spans [p, p+1]); the engine's rotation convention (SDF path, CPU pivot math,
// picking inverses) rotates about the authored position itself — the center
// of that mass. The raster therefore renders every cell position displaced by
// -h (h = kVoxelRasterCellAnchor, the half cell) so the rendered mass rotates
// about the authored lattice instead of orbiting it. iso(0.5,0.5,0.5) ==
// (0,0), so the correction is an exact no-op at yaw 0. Two forms, split by
// consumer: pos3DtoPos2DIsoYawedCellAnchor for PLACEMENT,
// yawedIsoDistanceCellAnchor for DEPTH. Exact world positions (SDF centers,
// entity translations) use the un-anchored pos3DtoPos2DIsoYawed /
// yawedIsoDistance.
//
// The correction is CONTINUOUS in yaw — do not quantize it. A whole-cell or
// whole-framebuffer-pixel quantization steps the whole layer at rounding
// crossings during a yaw sweep (1.9-15px single-frame jumps), while the
// continuous form's only cost is sub-pixel coverage-phase noise on the pinned
// centroid, equal in amplitude to the ordinary coverage noise.
const vec3 kVoxelRasterCellAnchor = vec3(0.5);

// Composite-depth displacement of the lower-corner raster lattice from an
// authored surface, in subdivided depth units. Odd densities round toward the
// deeper key so an analytic surface never sorts in front of its voxel twin.
int cardinalRasterLatticeDepthOffset(int subdivisions) {
    return (3 * subdivisions + 1) / 2;
}

vec2 pos3DtoPos2DIsoYawedCellAnchor(vec3 rasterPos, float visualYaw) {
    return pos3DtoPos2DIsoYawed(rasterPos - kVoxelRasterCellAnchor, visualYaw);
}

// Continuous-yaw iso depth — the camera-forward distance of a world point under
// a continuous Z-yaw camera: pos3DtoDistance(R_z(-visualYaw) * worldPos) =
// x(cos-sin) + y(sin+cos) + z. Smaller = nearer (GL_LESS). THE shared composite
// depth metric for every world surface under smooth yaw: the SDF smooth path
// (c_shapes_to_trixel), scatterCompositeDepthKey, and the detached
// composite (CPU twin IRMath::pos3DtoDistanceYawed) all derive their final
// occlusion depth from this one function, so SDF + voxels + detached stay
// co-sorted at EVERY yaw — not just cardinals. At a cardinal pose it collapses
// to the un-yawed x+y+z (pos3DtoDistance). CPU mirror:
// IRMath::pos3DtoDistanceYawed; Metal twin in ir_iso_common.metal.
float yawedIsoDistance(vec3 worldPos, float visualYaw) {
    float c = cos(visualYaw);
    float s = sin(visualYaw);
    return worldPos.x * (c - s) + worldPos.y * (s + c) + worldPos.z;
}

// Cell-anchor twin of yawedIsoDistance: composite depth of a voxel-raster
// cell/face position, measured at its authored-lattice world point
// (rasterPos - half cell) so voxel and SDF surfaces at the same world location
// carry the SAME yawed depth and co-sort exactly at every residual. Cardinal
// gather depths come from the integer store, not from this.
float yawedIsoDistanceCellAnchor(vec3 rasterPos, float visualYaw) {
    return yawedIsoDistance(rasterPos - kVoxelRasterCellAnchor, visualYaw);
}

// Exact (unquantized) composite depth key for a forward-scattered face: the
// true yawed camera-space iso depth of the recovered face origin, in the
// cardinal encodeDepthWithFace scale (× kDepthEncodeShift + slot) so it stays
// comparable with the quantized integer keys other composite writers (the SDF
// smooth-yaw path) emit. Do not round it: a rounded key (roundHalfUp of the
// yawed sum) ties adjacent micro-cells along a foreshortened in-plane axis on
// integer depth whenever |cos-sin| or |sin+cos| < 1, and GL_LESS resolves an
// equal-depth overlap by draw order — which runs AGAINST the depth gradient on
// the sign-flip side of a bracket (e.g. yaw > 45 deg, cos-sin < 0), so the
// farther quad wins its dilation overlap band (wrong-voxel-color bands at
// voxel boundaries). A continuous key makes geometric ties measure-zero.
// Shared by every forward-scatter composite writer — do not inline per-shader
// copies.
float scatterCompositeDepthKey(vec3 origin, float visualYaw, int slot) {
    return yawedIsoDistance(origin, visualYaw) * float(kDepthEncodeShift) + float(slot);
}

// Conservative XY growth of an axis-aligned half-extent swept under a Z-yaw of
// (cosYaw, sinYaw): each in-plane axis grows to |c|*hX + |s|*hY, Z unchanged.
// CPU mirror: IRMath::yawGrownIsoHalfExtent. Keeps the SDF/voxel iso-cull
// footprint identical on both sides.
vec3 yawGrownIsoHalfExtent(vec3 halfExtent, float cosYaw, float sinYaw) {
    float absC = abs(cosYaw);
    float absS = abs(sinYaw);
    return vec3(halfExtent.x * absC + halfExtent.y * absS,
                halfExtent.x * absS + halfExtent.y * absC,
                halfExtent.z);
}

// 2x2 deformation matrix that maps a face's un-yawed iso-pixel offset to the
// offset under residual yaw `residualYaw` (in [-pi/4, pi/4]).
//
// Derivation: each face contributes one "u" tangent (in-plane, rotates with
// world Z-yaw) and one "v" tangent (along world Z, fixed under Z-yaw). The
// returned mat2 D = M_phi * M_0^-1 post-multiplies an iso-pixel offset
// emitted at the cardinal rasterYaw to recover its position under the
// continuous yaw. At residualYaw == 0 all three are identity, so the
// cardinal-snap path stays bit-identical to the un-yawed projection.
//
// `face` uses the kXFace / kYFace / kZFace integer convention; other values
// return identity. CPU mirror: IRMath::faceDeformationMatrix.
mat2 faceDeformationMatrix(int face, float residualYaw) {
    float c = cos(residualYaw);
    float s = sin(residualYaw);
    if (face == kXFace) {
        return mat2(c - s, 1.0 - (c + s), 0.0, 1.0);
    }
    if (face == kYFace) {
        return mat2(c + s, c - s - 1.0, 0.0, 1.0);
    }
    if (face == kZFace) {
        return mat2(c, -s, s, c);
    }
    return mat2(1.0, 0.0, 0.0, 1.0);
}

// Residual-yaw-deformed trixel iso-pixel offset within the 2x3 face diamond.
// Applies faceDeformationMatrix to the un-yawed offset from faceOffset_2x3
// and rounds back to integer iso pixels via roundHalfUp so CPU and GPU
// resolve half-integer drift to the same cell.
//
// `subPixel` is 0 or 1; `face` uses the kXFace / kYFace / kZFace convention.
// CPU mirror: IRMath::deformedTrixelIsoPixel.
ivec2 deformedTrixelIsoPixel(int face, int subPixel, float residualYaw) {
    ivec2 unyawed = faceOffset_2x3(face, subPixel);
    mat2 D = faceDeformationMatrix(face, residualYaw);
    vec2 deformed = D * vec2(unyawed);
    return ivec2(roundHalfUp(deformed.x), roundHalfUp(deformed.y));
}

// Rotates vector v by unit quaternion q = (qx, qy, qz, qw).
// CPU mirror: IRMath::rotateVectorByQuat.
vec3 rotateByQuat(vec3 v, vec4 q) {
    vec3 u = q.xyz;
    float w = q.w;
    vec3 t = 2.0 * cross(u, v);
    return v + w * t + cross(u, t);
}

// Rotates vector v by the inverse (conjugate) of unit quaternion q.
vec3 rotateByInverseQuat(vec3 v, vec4 q) {
    return rotateByQuat(v, vec4(-q.xyz, q.w));
}

// The two in-plane unit model axes (e_u, e_v) a face's scatter quad spans, by
// axis = faceId >> 1 (0=X spans y,z; 1=Y spans x,z; 2=Z spans x,y) — matching
// faceSpanCorner's cornerSel.x -> e_u, cornerSel.y -> e_v ordering. Returned in
// `eu`/`ev` out-params (GLSL/Metal both pass by reference).
void faceInPlaneUnitAxes(int axis, out vec3 eu, out vec3 ev) {
    eu = (axis == 0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    ev = (axis == 2) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
}

// In-plane iso-pixel unit steps (su, sv) for a face's two in-plane world axes —
// the iso directions along which a re-voxelized cell's in-plane neighbour cells
// sit on screen. The detached re-voxelize raster dilates each surface face's
// footprint by ±su / ±sv so the sub-cell gaps round-to-cell leaves between
// adjacent rotated cells fill with the nearest (occlusion-winning,
// correct-colour) surface face. The two in-plane axes project
// to (±1, ∓1) and (0, ±2) iso pixels; normalising to ~1px keeps the dilation one
// pixel per side, so the silhouette grows by at most a pixel ALONG the surface
// and never across a concave notch (that direction is the face normal, untouched).
void faceInPlaneIsoSteps(int faceId, out ivec2 su, out ivec2 sv) {
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    su = roundHalfUp(normalize(vec2(pos3DtoPos2DIso(ivec3(eu)))));
    sv = roundHalfUp(normalize(vec2(pos3DtoPos2DIso(ivec3(ev)))));
}

// Per-axis scatter quantizes final depth to a 16-step band and injects a
// priority-major face/cell code (rank2 << 2 | cell2) into that band. The code
// spans 0..15, so the band width and tie step must match the CPU assertion in
// ir_render_types.hpp and the final-depth helper in ir_scatter_depth.glsl.
const float kScatterCellTieStep = 1.0 / 8388608.0;
const float kScatterCellTieBand = 16.0 * kScatterCellTieStep;

// Builds the local->world matrix from an SQT triple (scale, quaternion
// rotation, translation). Composition is T * R * S: local p maps to
// R * (S * p) + t — the same ordering SYSTEM_PROPAGATE_TRANSFORM uses when
// composing parent and child transforms. Quaternion layout matches the
// engine canon: vec4(qx, qy, qz, qw) with .w the scalar; identity is
// (0, 0, 0, 1). CPU mirror: IRMath::sqtToMat4.
mat4 sqtToMat4(vec3 scaleVec, vec4 rotationQuat, vec3 translation) {
    float x = rotationQuat.x;
    float y = rotationQuat.y;
    float z = rotationQuat.z;
    float w = rotationQuat.w;
    // mat3 R from unit quaternion (column-major).
    vec3 col0 = vec3(1.0 - 2.0 * (y * y + z * z),
                     2.0 * (x * y + w * z),
                     2.0 * (x * z - w * y)) * scaleVec.x;
    vec3 col1 = vec3(2.0 * (x * y - w * z),
                     1.0 - 2.0 * (x * x + z * z),
                     2.0 * (y * z + w * x)) * scaleVec.y;
    vec3 col2 = vec3(2.0 * (x * z + w * y),
                     2.0 * (y * z - w * x),
                     1.0 - 2.0 * (x * x + y * y)) * scaleVec.z;
    return mat4(
        vec4(col0, 0.0),
        vec4(col1, 0.0),
        vec4(col2, 0.0),
        vec4(translation, 1.0)
    );
}

// Applies an SRT (or any affine) matrix to an integer voxel grid cell,
// returning the destination integer cell with half-up rounding (re-rasterizes
// authored voxels into world-grid cells under a parent or local transform).
// CPU mirror: IRMath::matrixApplyToVoxelGrid.
ivec3 matrixApplyToVoxelGrid(mat4 transformMat, ivec3 cell) {
    vec4 worldPos = transformMat * vec4(vec3(cell), 1.0);
    return roundHalfUp(vec3(worldPos));
}

// Smooth analytic vision-circle reveal for one fog disc at `worldXY`.
// `circle` = (centerX, centerY, radius, edgeSoftness) in world units; `aa` is
// an extra half-width the band is widened to. Pass `aa > 0`: with a hard disc
// (edgeSoftness 0) and `aa == 0` the band has zero width and smoothstep is
// undefined — a binary per-column disc test goes through
// fogDiscRevealAtDistance (ir_voxel_face_select.glsl) instead. Returns 1.0
// fully revealed, 0.0 fully hidden.
float fogVisionCircleReveal(vec2 worldXY, vec4 circle, float aa) {
    const float dist = length(worldXY - circle.xy);
    const float a = max(circle.w, aa);
    return 1.0 - smoothstep(circle.z - a, circle.z + a, dist);
}

const int kDetachedFaceMissDepth = 2147483647;

const uint kSourceLightingBaked = 0u;
const uint kSourceLightingLinear = 1u;
const uint kSourceLightingHDR = 2u;
const uint kSourceLightingShadow = 3u;
const uint kSourceLightingAOShadow = 4u;

// owner.z selects color interpretation; owner.xy retain packed entity identity.
struct SourceVoxelFace {
    vec4 centerAndFace;
    vec4 color;
    uvec4 owner;
    vec4 directSunAndExposure;
    vec4 worldCenterAndAO;
};

struct DetachedFaceFootprint {
    vec2 origin;
    vec2 planeOrigin;
    vec2 uvOrigin;
    vec2 edgeU;
    vec2 edgeV;
    vec3 depth;
    ivec2 lo;
    ivec2 hi;
};

DetachedFaceFootprint detachedFaceFootprint(
    vec3 position, int faceId, int subdivisions, int microIndex,
    mat2 deformX, mat2 deformY, vec3 depthAxis, ivec2 frameOffset
) {
    const vec2 basisX = deformY * vec2(-1.0, -1.0);
    const vec2 basisY = deformX * vec2(1.0, -1.0);
    const vec2 basisZ = deformX * vec2(0.0, 2.0);
    const int axis = faceId >> 1;
    const int axisU = axis == 0 ? 1 : 0;
    const int axisV = axis == 2 ? 1 : 2;
    vec3 source = position * float(subdivisions) - vec3(0.5 * float(subdivisions));
    source[axis] += float((faceId & 1) * subdivisions);
    source[axisU] += float(microIndex / subdivisions);
    source[axisV] += float(microIndex % subdivisions);
    DetachedFaceFootprint face;
    face.origin = vec2(frameOffset) + vec2(float(subdivisions)) +
        source.x * basisX + source.y * basisY + source.z * basisZ;
    face.edgeU = axisU == 0 ? basisX : basisY;
    face.edgeV = axisV == 1 ? basisY : basisZ;
    const vec2 basisAxis = axis == 0 ? basisX : (axis == 1 ? basisY : basisZ);
    face.planeOrigin = vec2(frameOffset) + vec2(float(subdivisions)) + source[axis] * basisAxis;
    face.uvOrigin = vec2(source[axisU], source[axisV]);
    face.depth = vec3(source[axis] * depthAxis[axis],
                      depthAxis[axisU], depthAxis[axisV]);
    face.lo = ivec2(floor(face.origin + min(face.edgeU, vec2(0.0)) +
                        min(face.edgeV, vec2(0.0)))) - ivec2(1);
    face.hi = ivec2(ceil(face.origin + max(face.edgeU, vec2(0.0)) +
                        max(face.edgeV, vec2(0.0)))) + ivec2(1);
    return face;
}

// Samples are centroids of the local triangles reconstructed by the fragment
// gather. Half-open face coordinates give adjacent micro-faces one shared edge.
int detachedFaceSampleDepth(DetachedFaceFootprint face, ivec2 pixel, int parity, int slot) {
    const float determinant = projectedFaceDeterminant(face.edgeU, face.edgeV);
    if (abs(determinant) < 1e-6) return kDetachedFaceMissDepth;
    const vec2 delta = localTrixelCellCentroid(pixel, parity) - face.planeOrigin;
    const vec2 uv = projectedFaceCoordinates(delta, face.edgeU, face.edgeV, determinant);
    if (any(lessThan(uv, face.uvOrigin)) || any(greaterThanEqual(uv, face.uvOrigin + vec2(1.0))))
        return kDetachedFaceMissDepth;
    return encodeDepthWithFace(int(floor(face.depth.x + dot(uv, face.depth.yz) + 0.5)), slot);
}


vec3 detachedFaceViewNormal(int faceId, mat2 deformX, mat2 deformY, vec3 depthAxis) {
    const int axis = faceId >> 1;
    const vec2 projected = axis == 0 ? deformY * vec2(-1.0, -1.0) :
        (axis == 1 ? deformX * vec2(1.0, -1.0) : deformX * vec2(0.0, 2.0));
    const float depth = depthAxis[axis];
    return normalize(isoPositionToPos3D(projected, depth)) * ((faceId & 1) == 0 ? -1.0 : 1.0);
}
