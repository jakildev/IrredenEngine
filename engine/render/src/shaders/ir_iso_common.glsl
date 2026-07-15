// Shared isometric math utilities for all trixel pipeline compute shaders.
// Included via #include "ir_iso_common.glsl" (resolved by the engine's
// shader preprocessor at compile time).

// Axis-only face indices (X / Y / Z axis, polarity-blind). The 3-face
// raster helpers (`faceOffset_2x3`, `faceMicroPositionFixed`,
// `faceDeformationMatrix`) operate on these — they're "the X-axis face"
// without distinguishing X_NEG vs X_POS, which is fine for the deformation
// matrix (axis-only) and for the diamond slot layout (slot is a workgroup
// label, not a polarity).
const int kXFace = 0;
const int kYFace = 1;
const int kZFace = 2;

// Polarity-aware six-face IDs — see `docs/design/voxel-face-rasterization.md`
// and the matching `IRMath::FaceId` enum in `engine/math/include/irreden/
// ir_math.hpp`. Used for the per-slot visible-triplet handshake via
// `FrameDataVoxelToCanvas::visibleFaceIds_` (#1278): the CPU resolves
// which three WORLD faces are camera-visible this frame and uploads
// their FaceId per visible-triplet slot; the shader uses the FaceId to
// gate on the exposed-face bit and to pick the six-face outward normal
// / micro-position. Bit positions intentionally line up with the
// occlusion bits in `IRComponents::VoxelFlags::kFaceOccluded*`:
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

int pos3DtoDistance(ivec3 position) {
    return position.x + position.y + position.z;
}

// Reconstruct 3D position from 2D iso coordinates and depth.
// The isometric depth axis (1,1,1) is perpendicular to the screen:
//   pos3DtoPos2DIso(p + d*(1,1,1)) == pos3DtoPos2DIso(p) for any d.
// Given (isoX, isoY) and depth d = x+y+z, (x,y,z) is uniquely determined.
vec3 isoPixelToPos3D(int isoX, int isoY, float depth) {
    float x = (2.0 * depth - 3.0 * float(isoX) - float(isoY)) / 6.0;
    float y = x + float(isoX);
    float z = (float(isoY) + 2.0 * x + float(isoX)) / 2.0;
    return vec3(x, y, z);
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
// round-trip of a stored 8-bit channel is a fixed point (#2334 overflow-face
// relight rewrites an entry's colorPacked in place through this).
uint packColor(vec4 c) {
    uvec4 q = uvec4(clamp(c, 0.0, 1.0) * 255.0 + 0.5);
    return q.r | (q.g << 8) | (q.b << 16) | (q.a << 24);
}

// PCG-flavored integer hash (low-collision, no FP precision loss). Cheap enough
// for per-thread shader use; quality is sufficient for visual jitter on the
// stateless particle path (T-163) and any other "I need a deterministic
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
// `flip` (#2207) marks a silhouette-riser face emitted with the OPPOSITE
// polarity of its slot's triplet face (faceId = visibleFaceIds[slot] ^ 1,
// the #2162 flip): lighting/AO/shadow decode it to negate the slot-derived
// outward normal instead of shading the riser with an inverted Lambert.
int encodeDepthWithFace(int rawDepth, int face, int flip) {
    return rawDepth * kDepthEncodeShift + (flip << 2) + face;
}

// Unflipped overload — the common case (SDF shapes, particles, resolve
// re-emits of unflipped cells, non-riser voxel faces).
int encodeDepthWithFace(int rawDepth, int face) {
    return encodeDepthWithFace(rawDepth, face, 0);
}

// Shared decode helpers — the ONLY places the two distance-encoding bit
// layouts live (#2207). Single-canvas: [31:3] depth | [2] flip | [1:0] slot.
// Per-axis (#1458, wFrac carrier for out-of-plane sub-cell position):
// [31:15] depth | [14:11] wFrac4 | [10] flip | [9:6] uFrac4 | [5:2] vFrac4
// | [1:0] slot. Depth decodes by arithmetic right shift (floor), so negative
// depths recover exactly; slot/flip/fracs are pure low-bit masks. wFrac sits
// directly below depth so atomicMin still orders by true plane depth (a
// same-cell nearer plane wins) before flip/frac/slot. Route every consumer
// through these — an open-coded shift is how a carrier migration silently
// mis-decodes.
int decodeSlot(int encoded) { return encoded & 3; }
int decodeFlipSingle(int encoded) { return (encoded >> 2) & 1; }
int decodeDepthSingle(int encoded) { return encoded >> 3; }
int decodeFlipPerAxis(int encoded) { return (encoded >> 10) & 1; }
int decodeDepthPerAxis(int encoded) { return encoded >> 15; }
// 4-bit sub-cell fracs (0..15, 8 = cell centre): u/v span the face's two
// in-plane axes; w is the OUT-OF-PLANE fraction along the face axis — the
// coordinate the integer cell lattice cannot carry. Dropping w reconstructs
// every face of fractionally-positioned content on the integer lattice
// plane, displacing it along its own normal by up to half a voxel (the
// "cubes stop being cubes under yaw" class).
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

// Two-tier composite depth partition (#1958). The most-negative
// kDepthForegroundBandWidth codes of [kMinTriangleDistance, kMaxTriangleDistance]
// are reserved for foreground-priority detached solids: the framebuffer gather
// (f_trixel_to_framebuffer) clamps WORLD content out of the band and pins
// FOREGROUND content into it, so a priority solid is unconditionally nearer than
// any world fragment regardless of world extent. The far edge is
// foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth. Mirrors
// IRRender::kDepthForegroundBandWidth (ir_render_types.hpp) and the .metal twin.
const int kDepthForegroundBandWidth = 16384;

// Per-trixel priority tiers (#1960). Subdivide the reserved foreground band into
// N-1 disjoint equal-width tiers; tier 0 = world (out of band). MORE-negative =
// higher priority, so tier N-1 sits at the near (most-negative) band edge.
// f_trixel_to_framebuffer selects `tier = max(perEntityTier, perTrixelTier)` per
// fragment, then pins enc into depthForegroundTier{Lo,Hi}, centered on
// depthForegroundTierCenter. Default tier 0 ⇒ byte-identical to #1958 master.
// Mirror IRRender::kDepthForegroundTier* (ir_render_types.hpp) + the .metal twin.
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

// Per-trixel priority carrier (#1960). The per-trixel tier rides the top K=2 bits
// of the 64-bit entity id stored in the triangleEntityIds channel (uvec2: .x =
// low word, .y = high word; the carrier is bits 30..31 of the high word). THE
// chokepoint: every reader masks via decodeEntityId, the stage-2 writer packs via
// encodeEntityIdWithPriority — no site open-codes the mask. Priority 0 ⇒ id
// unchanged. Mirror IRRender::kEntityIdPriority* (ir_render_types.hpp) + .metal.
const uint kEntityIdPriorityShiftInHighWord = 30u;
const uint kEntityIdPriorityMaskInHighWord = 0x3u << kEntityIdPriorityShiftInHighWord;
// Fog cut-face carrier (#2124 lit-cross-section follow-up): the bit just below
// the priority tier (bit 29 of the high word) flags a fog cross-section CUT face
// so LIGHTING_TO_TRIXEL can force it fully lit — no self-shadow from the fog-
// hidden neighbor voxels, no interior-crease AO — the "lit as a clean exposed
// face" cross-section spec (supersedes the epic's option-1 full-AO/shadow
// default). Rides the SAME masking chokepoint as the priority tier:
// kEntityIdHighWordMask strips it, so every id READER (picking) ignores it.
// Default (non-cut) ⇒ the stored id is unchanged, so non-fog scenes stay
// byte-identical.
const uint kEntityIdCutFaceMaskInHighWord = 0x1u << 29u;
const uint kEntityIdHighWordMask =
    ~(kEntityIdPriorityMaskInHighWord | kEntityIdCutFaceMaskInHighWord);
uint decodePriority(uvec2 rawId) {
    return (rawId.y >> kEntityIdPriorityShiftInHighWord) & 0x3u;
}
bool decodeCutFace(uvec2 rawId) {
    return (rawId.y & kEntityIdCutFaceMaskInHighWord) != 0u;
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
uvec2 encodeEntityIdCutFace(uvec2 packed, bool isCutFace) {
    return isCutFace ? uvec2(packed.x, packed.y | kEntityIdCutFaceMaskInHighWord)
                     : packed;
}

// Per-axis fractional encoding (#1458, flip carrier #2207, wFrac carrier):
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
// should call `faceOutwardNormal6` with the per-slot `visibleFaceIds[slot]`
// from the UBO instead of the slot itself. This 3-face overload is kept
// for callers that genuinely want the axis-only X_NEG/Y_NEG/Z_NEG normals
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
// when the matching neighbor is active, so the face should NOT emit.
// Per the design doc's exposed-face gate (`emit ⟺ visible ∧ exposed`).
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
// the 3-face `faceMicroPositionFixed` above). The other two axes sweep
// `u, v ∈ [0, subdivisions)` exactly as the 3-face overload does.
// Used by the subdivided emit path in `c_voxel_to_trixel_stage_{1,2}`
// after the per-slot world `faceId = visibleFaceIds[slot]` lookup (#1278).
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
// rounding bit-identical across the CPU/GPU boundary (#1462).
int isoDepthAlongAxis(ivec3 pos, vec3 axis) {
    return roundHalfUp(dot(vec3(pos), axis));
}

ivec2 trixelOriginOffsetX1(ivec2 trixelCanvasSize) {
    return trixelCanvasSize / ivec2(2);
}

ivec2 trixelOriginOffsetZ1(ivec2 trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + ivec2(-1, -1);
}

int trixelOriginModifier(ivec2 trixelCanvasOffsetZ1, vec2 frameCanvasOffset) {
    vec2 canvasOffsetFloored = floor(frameCanvasOffset);
    return (trixelCanvasOffsetZ1.x + trixelCanvasOffsetZ1.y +
            int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)) & 1;
}

// --- Trixel-cell diagonal split: which of the two triangles does this cover? ---
// The trixel->framebuffer gather (f_trixel_to_framebuffer) samples the canvas
// at `origin = TexCoords * textureSize`. Each iso texel-cell holds two triangles
// split along a diagonal; this resolves which half a fragment covers by
// conditionally decrementing `origin.y` one row (parity bit + a sub-pixel
// `fract` test). It only ever adjusts `.y`, and is byte-identical to CPU
// `pos2DIsoToTriangleIndex` (ir_math.cpp) — the picking/hover path reuses it so
// GPU and CPU agree on which trixel the mouse is over.
//
// GL applies this shift to the color/depth/id reads; Metal reads color/depth
// from the raw origin, because its negated clip-Y (top-left target vs GL's
// bottom-left framebuffer) already lands the raw sample on the correct row.
// Both backends read the CORRECT trixel for their own raster convention.
// Picking is the shared exception: BOTH backends shift the hover coordinate,
// because it must match CPU `pos2DIsoToTriangleIndex` (raster-Y-independent),
// even though only GL shifts the color/depth gather. See #442;
// docs/design/trixel-parity-shift-442-investigation.md.
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

// NOTE (#1944): the per-axis camera-pan anchor is `trixelOriginOffsetZ1(size) +
// ivec2(floor(frameCanvasOffset))` — the WHOLE-iso camera offset, NOT the
// density-scaled `trixelFrameOffset` above (per-axis canvases are
// base-resolution since #1458, so the scaled anchor jittered under pan; see
// system_trixel_to_framebuffer.hpp drawPerAxisScatter). It is INLINED at each
// per-axis site rather than centralised here so this shared header does not gain
// a symbol — adding to ir_iso_common perturbs the cardinal SDF/voxel shaders'
// FP scheduling and drifts their byte-identical fast path (the same reason
// perAxisCellToWorld3D lives in ir_per_axis_lighting, not here).

ivec2 trixelCanvasPixelToIsoRel(
    ivec2 pixel,
    ivec2 trixelCanvasOffsetZ1,
    vec2 frameCanvasOffset,
    ivec2 voxelRenderOptions
) {
    return pixel - trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
}

// Cardinal Z-yaw helpers (T-055).
// FrameDataVoxelToTrixel.rasterYaw is guaranteed to be a multiple of pi/2 by
// the camera-side split helper (engine/prefabs/irreden/render/camera.hpp); the
// renderer uses one of four basis-vector permutations selected by an integer
// index in [0, 3] so integer voxel positions still land on integer trixel
// pixels post-rotation. residualYaw is absorbed by faceDeform[] in the trixel
// emit (T-293); the screen-space composite pass was retired by T-323. These
// helpers ignore it.
//
// Sign convention: rotateCardinalZ is world->view = R_z(-rasterYaw) — same as
// the continuous-yaw matrix in c_shapes_to_trixel.glsl (T-056). At
// visualYaw=+pi/2 the camera turns +90 deg around +Z; from the view's POV the
// world appears to spin -90 deg, so world (+X,0,0) lands at view (0,-Y,0) and
// projects to iso (-1,+1). Voxels (this helper) and shapes (T-056) MUST share
// this convention or they desync at non-zero yaw.

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
// IRMath::cardinalYawCosSin; retires the open-coded cardinalCos/cardinalSin
// tables that callers used to inline.
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

// View-space lower-corner shift applied after rotateCardinalZ so the
// rotated unit voxel's view-space AABB lower corner equals the rotated
// voxel position. R_z permutes/negates axes; for the unit voxel [0,1]^3
// the post-rotation AABB lower corner relative to the rotated origin is:
//   cardinal 0: (0, 0, 0)
//   cardinal 1: (0,-1, 0)  (world x in [0,1] -> view y in [-1, 0])
//   cardinal 2: (-1,-1, 0)
//   cardinal 3: (-1, 0, 0)
// Adding this shift keeps the diamond 2x3 emit aligned with the voxel's
// view-space iso footprint at every cardinal. At cardinal 0 the shift is
// zero so the cardinal-snap path stays bit-identical to master.
ivec3 cardinalLowerCornerShift(int cardinalIndex) {
    if (cardinalIndex == 1) return ivec3(0, -1, 0);
    if (cardinalIndex == 2) return ivec3(-1, -1, 0);
    if (cardinalIndex == 3) return ivec3(-1, 0, 0);
    return ivec3(0, 0, 0);
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

// Convenience wrapper for T-057 (picking inverse). T-058 (screen-space residual
// pass) was retired by T-323 — residual yaw lives in faceDeform[] (T-293).
// Not consumed by the current T-055 shaders; scaffolded here so consuming tasks
// can reference it from ir_iso_common directly.
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
        // Undo the rasterizer's `cardinalLowerCornerShift` (applied in
        // world units after division by scale) before rotating back to
        // world coordinates.
        pos3D -= vec3(cardinalLowerCornerShift(cardinalIndex));
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

// Smooth-camera-yaw inverse (#1719) of the #1345 smooth-yaw SDF store: those
// pixels are placed at roundHalfUp(pos3DtoPos2DIsoYawed(world, visualYaw))
// with the VIEW-frame iso depth (#1370), so recover the view-frame point with
// the cardinal-frame solver and rotate back by the full +visualYaw. No
// lower-corner shift — the smooth store never applies one. Identical to
// trixelCanvasPixelToWorld3D at visualYaw == 0 (cos=1/sin=0, cardinal 0 takes
// the same shift-free path), keeping the cardinal fast path byte-identical.
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

// Continuous-yaw + per-face deformation math (T-292; consumed by T-293).
// Mirrors IRMath::pos3DtoPos2DIsoYawed / faceDeformationMatrix /
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

// Exact (unquantized) composite depth key for a forward-scattered face: the
// true yawed camera-space iso depth of the recovered face origin, kept in the
// cardinal encodeDepthWithFace scale (xkDepthEncodeShift + slot) so it stays comparable with
// the quantized integer keys other composite writers (the SDF smooth-yaw path)
// emit. The quantization this replaces (roundHalfUp of the yawed sum) made
// adjacent micro-cells along a foreshortened in-plane axis TIE on integer
// depth whenever |cos-sin| or |sin+cos| < 1, and GL_LESS resolves an
// equal-depth overlap by draw order — which runs AGAINST the depth gradient
// on the sign-flip side of a bracket (e.g. yaw > 45 deg, cos-sin < 0), so the
// farther quad won its dilation overlap band: the #1457 wrong-voxel-color
// bands at voxel boundaries. A continuous key makes geometric ties
// measure-zero, so the depth test orders every overlap correctly at every
// residual. Shared by every forward-scatter composite writer — do not inline
// per-shader copies.
//
// Continuous-yaw iso depth — the camera-forward distance of a world point under
// a continuous Z-yaw camera: pos3DtoDistance(R_z(-visualYaw) * worldPos) =
// x(cos-sin) + y(sin+cos) + z. Smaller = nearer (GL_LESS). THE shared composite
// depth metric for every world surface under smooth yaw: the SDF smooth path
// (c_shapes_to_trixel), the scatterCompositeDepthKey below, and the detached
// composite (CPU twin IRMath::pos3DtoDistanceYawed) all derive their final
// occlusion depth from this one function, so SDF + voxels + detached stay
// co-sorted at EVERY yaw — not just cardinals. At a cardinal pose it collapses
// to the un-yawed x+y+z (pos3DtoDistance), so the cardinal fast path stays
// byte-identical. CPU mirror: IRMath::pos3DtoDistanceYawed; Metal twin in
// ir_iso_common.metal.
float yawedIsoDistance(vec3 worldPos, float visualYaw) {
    float c = cos(visualYaw);
    float s = sin(visualYaw);
    return worldPos.x * (c - s) + worldPos.y * (s + c) + worldPos.z;
}

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
// sit on screen. The detached re-voxelize raster (#1557) dilates each surface
// face's footprint by ±su / ±sv so the sub-cell gaps round-to-cell leaves
// between adjacent rotated cells fill with the nearest (occlusion-winning,
// correct-colour) surface face — conservative coverage à la the per-axis scatter
// (#1494), adapted to the cardinal-0 compute emit. The two in-plane axes project
// to (±1, ∓1) and (0, ±2) iso pixels; normalising to ~1px keeps the dilation one
// pixel per side, so the silhouette grows by at most a pixel ALONG the surface
// and never across a concave notch (that direction is the face normal, untouched).
void faceInPlaneIsoSteps(int faceId, out ivec2 su, out ivec2 sv) {
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    su = roundHalfUp(normalize(vec2(pos3DtoPos2DIso(ivec3(eu)))));
    sv = roundHalfUp(normalize(vec2(pos3DtoPos2DIso(ivec3(ev)))));
}

// Default conservative-coverage margin (framebuffer pixels) the per-axis
// forward-scatter grows each quad by along each screen edge normal (#1494).
// ~0.85px reliably closes the sub-pixel thin-sliver waffle while keeping the
// silhouette/over-fill within a fraction of a pixel.
const float kScatterDilateMarginPx = 0.85;

// Depth penalty (in the x4+slot composite-key scale) a scatter fragment in the
// conservative-dilation MARGIN adds, so a margin only fills pixels no exact
// footprint claims (#1457). Two cells of the same face plane carry identical
// per-fragment planar depth, so without the bias their margin-vs-interior
// overlap is an exact tie decided by draw order — wrong-voxel-color bands on
// the sign-flip side of a bracket. 0.25 key units = 1/32 world unit: beats
// exact ties, far below any off-knife-edge separation between distinct planes
// (>= 8*|cos-sin| key units), so genuine occlusion is never reordered.
const float kScatterMarginDepthBiasKey = 0.25;

// Deterministic sub-band tiebreak (#2255 determinism, #2411 priority order).
// Wherever two scattered quads' final depths land within one float ULP of
// each other, GL_LESS keeps the first-drawn fragment — and draw order is
// instance order, which is the #1961 cell-compaction's atomic-append order:
// run-variant. Two realized tie classes:
//   * SAME-AXIS margin-yield crossover (#2255): a near face's margin ramp
//     crosses its parallel neighbor's exact plane (same axis, one in-plane
//     world step) and the crossover pixel ties bit-exactly.
//   * CROSS-AXIS band ties (#2411): two non-parallel face-plane depth fields
//     always cross; in the band-wide strip around the crossing the winner
//     used to be the lower CELL code — semantically arbitrary across axis
//     canvases, parity-structured, hence per-cell Lambert alternation on
//     contiguous moving content (and the shared-edge checkerboard fringe).
// Fix: quantize the final fragment depth to a coarse band and inject a
// PRIORITY-MAJOR, CELL-MINOR 4-bit code into the sub-band bits:
//   depth = floor(depth / band) * band + code * kScatterCellTieStep
//   code  = (rank2 << 2) | cell2
//   rank2 = flip ? 3 : slot        (2 bits — unflipped slots 0..2 in cardinal
//                                   atomicMin low-bit order; ALL flipped
//                                   emergency faces collapse to rank 3 and
//                                   fall to cell2. 6 face states don't fit in
//                                   2 bits, and the band cannot widen to carry
//                                   a 3rd — see PRECONDITION — so the collapse
//                                   is forced, not a preference.)
//   cell2 = (ij.x & 1) | (ij.y & 2) (2 bits — distinct for every same-plane /
//                                   parallel-plane neighbor pair: in-plane
//                                   steps only project to iso-diagonal
//                                   (+/-1,+/-1), which flips x&1, or (0,+/-2),
//                                   which flips y&2; (+/-2,0) cannot occur)
// What the code separates, by tie class:
//   * flipped vs unflipped — rank 3 vs 0..2 always separates.
//   * unflipped cross-axis (the #2411 class) — distinct slots by construction
//     (slot IS the axis canvas), so the whole crossing strip resolves by slot
//     rank, consistently, no parity alternation; mirrors the cardinal encode's
//     (flip<<2)|slot low bits.
//   * same-slot ties, incl. the #2255 same-axis margin-yield crossover — fall
//     to cell2, whose in-plane-step proof above covers exactly this
//     same-plane / parallel-plane case. Determinism contract preserved.
//   * flipped vs flipped on DIFFERENT slots — NOT proven distinct. Both
//     collapse to rank 3 and fall to cell2, but their ij index different axis
//     canvases, so the in-plane-step enumeration does not cover the pair: the
//     codes differ only if the ij happen to, and on collision the winner is
//     draw order (the run-variant #2255 class). Master's 3-bit cell code had
//     the same cross-axis hole; this narrows it to one rare class (co-rotated
//     flipped risers) while giving the common unflipped cross-axis case a
//     provable separation. Revisit if that class stops being rare (#2411).
// kScatterCellTieStep = 2^-23: >= 1 float32-depth ULP for depth < 1 AND 2
// quanta of a 24-bit fixed depth buffer, so the code survives quantization on
// both backends. Band = 16 steps = 2^-19 ~= 0.25 key units at the default
// range.
// PRECONDITION — two mutually-opposed halves, both asserted CPU-side in
// ir_render_types.hpp (kScatterCellTieBandSteps); a shader cannot assert:
//   (a) margin-vs-exact — the margin bias must land a margin fragment at least
//       one full band behind its same-plane exact owner after floor
//       quantization:
//         kScatterMarginDepthBiasKey * subScale / depthRange >= kScatterCellTieBand
//       i.e. depthRange <= subScale * 2^17 (default 131070 < 131072 — a 2-unit
//       margin). subScale is floor-clamped to 1.0 by the vertex stage and the
//       bias is linear in it, so subScale 1 is the worst case. WIDENING the
//       band TIGHTENS this half.
//   (b) code-fits-in-band — maxCode <= bandSteps - 1, i.e. 15 <= 15: exact,
//       zero slack. WIDENING the band RELAXES this half.
// Together they bracket the band to [16, 16.0002], so 16 is the UNIQUE
// admissible width — which is what forces rank2's collapse above. A pass that
// adds tie levels (a 3-bit rank, #2428's fractional-edge work) pushes maxCode
// to 23, needing a 32-step band, which in turn needs depthRange <= subScale *
// 2^16 = 65536 while it is 131070: the widening that fixes the code overflow
// is exactly the one that breaks (a). Such a pass must move the depth range or
// the encode, not just the band.
// Slot (1.0 key unit = 4 bands) and plane (>= 8 key units off-knife-edge)
// separations remain multiple bands, so no genuine occlusion is reordered;
// only tie-band pixels gain a deterministic, priority-ordered winner.
const float kScatterCellTieStep = 1.0 / 8388608.0;
// Derived, not retunable alone: 16 is pinned by the PRECONDITION above and
// asserted CPU-side (kScatterCellTieBandSteps, ir_render_types.hpp). Exact
// power-of-two product, so the derivation is bit-identical to the literal.
// The overflow lane's two-band bias derives from this in turn
// (v_peraxis_scatter.glsl / metal/peraxis_scatter.metal: 2.0 * band).
const float kScatterCellTieBand = 16.0 * kScatterCellTieStep;

// Margin-yield gradient scale (#1883). The flat bias above only breaks SUB-PIXEL
// same-plane ties. Once the per-axis margin grows large on a foreshortened face
// (iter-1's 0.5*|n| reaches a cell-deep fraction), the margin EXTRAPOLATES the
// face plane far enough that its depth beats a NEIGHBORING face's exact footprint
// along a shared ridge — the #1883 doubled top<->side sliver: the over-grown
// top-face margin won a ~cell-wide band on the side face below the ridge. The fix
// is to make a margin yield in proportion to how far it reached: scale the yield
// by the fragment's own extrapolation excursion (penetration past the exact
// footprint x the per-axis screen-depth gradient). A sub-pixel gap-fill barely
// yields (still wins background and cross-cube silhouette overlaps); a cell-deep
// margin yields hard (loses the ridge to the neighbor's exact footprint). 3
// covers the worst-case symmetric two-plane depth divergence near a cardinal with
// headroom. Folded into the per-axis yield-grad varying by the scatter vertex
// stage, so the fragment stage needs no copy of this constant.
const float kScatterMarginYieldGradScale = 3.0;

// Miter limit for the conservative dilation below (#1538): caps how far a sharp
// (acute) sliver corner is allowed to extend, in multiples of marginPx. Bounds
// the over-fill so a foreshortened cell's tip can't shoot off into a blob while
// still letting every corner move outward enough to close the inter-cell cracks.
const float kScatterMiterLimit = 2.0;

// Pitch-proportional coverage fraction for the DETACHED forward-scatter (#1538).
// The detached cubes leave black seams between adjacent cells / where the
// visible faces meet under an off-snap residual — a real gap (measured 2-6px on
// a ~5-8px-pitch cube) that scales with the on-screen cell PITCH (the projected
// unit-axis length), not a fixed sub-pixel crack. Closing it with a fixed margin
// needs a few px, which over-grows SMALL on-screen cubes into blobs (observed).
// A margin set to this fraction of the cell pitch instead tracks the gap at
// every scale — it closes the seam on a large cube where the gap is widest and
// shrinks to a sliver on a tiny cube where the fixed kScatterDilateMarginPx
// floor takes over, so small cubes never blob. Used as a floor against
// kScatterDilateMarginPx in the detached scatter only (camera-path scatter keeps
// the fixed margin — its world canvas isn't the small-cube regime this
// addresses). CPU has no mirror (shader-only).
const float kScatterDetachedPitchFraction = 0.5;

// Conservative screen-space coverage for the per-axis forward-scatter (#1494,
// #1538). Each non-empty cell scatters one deformed face rhombus; at off-snap
// residual poses the rhombus foreshortens toward a sub-pixel-thin sliver that
// slips between fragment centers and drops out under pixel-center rasterization.
// A linear iso-of-rotation map of the gap-free unit-cell tiling is gap-free in
// CONTINUOUS space, but that guarantee does not survive finite-resolution
// rasterization of a sub-pixel polygon, so each quad is grown outward.
//
// `su`/`sv` are the face's two in-plane unit axes projected to framebuffer
// pixels; the margin is a fixed pixel amount, so it is negligible at large
// on-screen size (silhouette unchanged) and is screen-space (independent of
// subdivision density / zoom) — unlike the rejected model-space ×2 quad span,
// which scales with size and over-fills. `cornerSign` is sign(aPos)
// (cornerSign.x -> e_u edge, .y -> e_v edge). Returns the clip-space (NDC)
// offset to add to the corner.
//
// MITER, not additive sum (#1538). The naive `marginPx*(e1+e2)` of the two edge
// normals CANCELS at a sliver's acute corner — there e1,e2 turn antiparallel, so
// the sum collapses to ~0 and the sharp tip is left un-grown. Those un-grown
// tips line up along the foreshortened lattice and leak: lattice-aligned black
// cracks + interior speckle on detached cubes (#1538). The miter
// marginPx*(e1+e2)/(1+dot(e1,e2)) is the displacement that moves BOTH edges out
// by marginPx (|δ| = marginPx/cos(halfAngle)); it equals the additive sum at a
// square corner (no change there) but keeps the acute tip moving outward instead
// of cancelling. Clamp |δ| to kScatterMiterLimit*marginPx so the sharpening
// 1/cos(halfAngle) can't blow a sliver tip into a blob (the failure mode of just
// raising marginPx). This is geometric: the fix is WHERE the margin lands per
// corner, not a bigger blunt margin.
vec2 scatterConservativeDilation(
    vec2 su, vec2 sv, vec2 cornerSign, float minMarginPx, vec2 ndcPerPx
) {
    // Outward normal of each edge = the component of the OTHER edge perpendicular
    // to it (so a thin sliver is grown across its thin dimension, not along it).
    // |nu|/|nv| are the on-screen perpendicular extents across each edge.
    vec2 nu = sv - su * (dot(sv, su) / max(dot(su, su), 1e-8));
    vec2 nv = su - sv * (dot(su, sv) / max(dot(sv, sv), 1e-8));
    bool hasU = dot(nu, nu) > 1e-10;
    bool hasV = dot(nv, nv) > 1e-10;
    if (!hasU && !hasV) return vec2(0.0);
    // Per-axis margin (#1883): grow each edge by half its OWN on-screen extent,
    // continuous, floored at minMarginPx for fragment-center coverage. The
    // collapsing axis grows (bridging the band gap at its sliver ends) while the
    // long silhouette edge stays at the tight floor — replacing the anisotropic
    // max(suLen,svLen) + hard degenSin gate that over-grew the long axis and
    // dashed the foreshortened silhouette.
    float marginU = max(minMarginPx, 0.5 * length(nu));
    float marginV = max(minMarginPx, 0.5 * length(nv));
    vec2 e1 = hasU ? cornerSign.y * normalize(nu) : vec2(0.0); // e_u edge normal
    vec2 e2 = hasV ? cornerSign.x * normalize(nv) : vec2(0.0); // e_v edge normal
    if (!hasU) return e2 * marginV * ndcPerPx;                 // only one edge -> plain push
    if (!hasV) return e1 * marginU * ndcPerPx;
    // Miter that moves edge-u out by marginU and edge-v by marginV: solve
    // [e1;e2]·δ = (marginU,marginV). Reduces to the #1538 equal-margin miter when
    // marginU==marginV.
    float det = e1.x * e2.y - e1.y * e2.x;
    // Exactly-antiparallel (180deg, degenerate flat corner): no stable solve —
    // push along the shared thin direction (perpendicular to the edges), clamped.
    if (abs(det) < 1e-4) {
        return vec2(-e1.y, e1.x) * (max(marginU, marginV) * kScatterMiterLimit) * ndcPerPx;
    }
    vec2 delta = vec2(
        e2.y * marginU - e1.y * marginV,
        e1.x * marginV - e2.x * marginU
    ) / det;
    // Clamp the miter so an acute corner can't blow a sliver tip into a blob
    // (the #1538 limit), relative to the larger contributing margin.
    float maxLen = kScatterMiterLimit * max(marginU, marginV);
    float dLen = length(delta);
    if (dLen > maxLen) delta *= maxLen / dLen;
    return delta * ndcPerPx;
}

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
// returning the destination integer cell with half-up rounding. Used by the
// GRID-mode rotation path (T-294) to re-rasterize authored voxels into
// world-grid cells under a parent or local transform. CPU mirror:
// IRMath::matrixApplyToVoxelGrid.
ivec3 matrixApplyToVoxelGrid(mat4 transformMat, ivec3 cell) {
    vec4 worldPos = transformMat * vec4(vec3(cell), 1.0);
    return roundHalfUp(vec3(worldPos));
}

// Smooth analytic vision-circle reveal for one fog disc, shared by
// FOG_TO_TRIXEL (per-pixel floor reveal) and VOXEL_TO_TRIXEL_STAGE_1 (per-voxel
// object clip) so the floor edge and the voxel-object edge are the SAME
// analytic curve (#2102) — one formula, no CPU/GPU or GL/Metal drift.
// `circle` = (centerX, centerY, radius, edgeSoftness) in world units; `aa` is
// an extra antialias half-width (FOG_TO_TRIXEL passes its per-pixel
// worldPerPixel for a zoom-stable rim; the voxel clip passes 0 for a binary
// inside/outside test — `reveal >= 0.5` is `worldXY inside radius` regardless
// of the softening width, since smoothstep is 0.5 at its midpoint). Returns
// 1.0 fully revealed, 0.0 fully hidden.
float fogVisionCircleReveal(vec2 worldXY, vec4 circle, float aa) {
    const float dist = length(worldXY - circle.xy);
    const float a = max(circle.w, aa);
    return 1.0 - smoothstep(circle.z - a, circle.z + a, dist);
}
