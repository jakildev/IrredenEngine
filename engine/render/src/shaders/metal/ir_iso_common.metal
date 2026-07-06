// Shared isometric math utilities for all trixel pipeline Metal compute
// shaders.  Mirrors shaders/ir_iso_common.glsl.  Resolved by the engine's
// Metal #include preprocessor at runtime, NOT by the standard preprocessor —
// header guards are still recommended for safety.
#ifndef IR_ISO_COMMON_METAL_INCLUDED
#define IR_ISO_COMMON_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

// Axis-only face indices (X / Y / Z axis, polarity-blind). The 3-face
// helpers use these; the 6-face FaceId constants below are the
// polarity-aware version used by visible-triplet rasterization (#1278).
constant int kXFace = 0;
constant int kYFace = 1;
constant int kZFace = 2;

// Polarity-aware six-face IDs (mirrors `kFaceXNeg`/... in the GLSL
// `ir_iso_common.glsl` and `IRMath::FaceId` in C++). Bit positions line
// up with `IRComponents::VoxelFlags::kFaceOccluded*`:
//   bit(faceId) = 2 + faceId
constant int kFaceXNeg = 0;
constant int kFaceXPos = 1;
constant int kFaceYNeg = 2;
constant int kFaceYPos = 3;
constant int kFaceZNeg = 4;
constant int kFaceZPos = 5;

inline int2 pos3DtoPos2DIso(int3 position) {
    return int2(
        -position.x + position.y,
        -position.x - position.y + 2 * position.z
    );
}

inline int pos3DtoDistance(int3 position) {
    return position.x + position.y + position.z;
}

// Reconstruct 3D position from 2D iso coordinates and depth.  The isometric
// depth axis (1,1,1) is perpendicular to the screen, so given (isoX, isoY)
// and depth d = x + y + z, (x, y, z) is uniquely determined.
inline float3 isoPixelToPos3D(int isoX, int isoY, float depth) {
    float x = (2.0 * depth - 3.0 * float(isoX) - float(isoY)) / 6.0;
    float y = x + float(isoX);
    float z = (float(isoY) + 2.0 * x + float(isoX)) / 2.0;
    return float3(x, y, z);
}

inline float3 isoToLocal3D(int2 isoRel, float depth) {
    return isoPixelToPos3D(isoRel.x, isoRel.y, depth);
}

inline float4 unpackColor(uint packedColor) {
    return float4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

// PCG-flavored integer hash (low-collision, no FP precision loss). Cheap
// enough for per-thread shader use; quality is sufficient for visual jitter
// on the stateless particle path (T-163). Mirrors the GLSL implementation
// in ir_iso_common.glsl line-for-line so deterministic seeds reproduce on
// both backends.
inline uint hash3(uint a, uint b, uint c) {
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

inline float3 randomUnitVec(uint seed) {
    const float kInvU32 = 1.0f / 4294967295.0f;
    uint rx = hash3(seed, 0x9E3779B1u, 0u);
    uint ry = hash3(seed, 0x85EBCA77u, 1u);
    uint rz = hash3(seed, 0xC2B2AE3Du, 2u);
    return float3(
        float(rx) * kInvU32 * 2.0f - 1.0f,
        float(ry) * kInvU32 * 2.0f - 1.0f,
        float(rz) * kInvU32 * 2.0f - 1.0f
    );
}

// Map local invocation ID within a (2, 3, 1) workgroup to a face type.
//   (0,0),(1,0) -> Z_FACE
//   (1,1),(1,2) -> X_FACE
//   (0,1),(0,2) -> Y_FACE
inline int localIDToFace_2x3(uint2 localId) {
    if (localId.y == 0u) return kZFace;
    if (localId.x == 1u) return kXFace;
    return kYFace;
}

// Face offset within the 2x3 trixel diamond for a given face and sub-pixel
// index (0 or 1).  Matches the layout used by localIDToFace_2x3():
//   Z -> (0,0),(1,0)   X -> (1,1),(1,2)   Y -> (0,1),(0,2)
inline int2 faceOffset_2x3(int face, int subPixel) {
    if (face == kZFace) return int2(subPixel, 0);
    if (face == kXFace) return int2(1, 1 + subPixel);
    return int2(0, 1 + subPixel);
}

// Encode depth with face priority for deterministic depth-test resolution.
// The *4 spacing ensures face indices never cross depth boundaries.
inline int encodeDepthWithFace(int rawDepth, int face) {
    return rawDepth * 4 + face;
}

// Two-tier composite depth partition (#1958). The most-negative
// kDepthForegroundBandWidth codes of [kMinTriangleDistance, kMaxTriangleDistance]
// are reserved for foreground-priority detached solids: trixel_to_framebuffer
// clamps WORLD content out of the band and pins FOREGROUND content into it, so a
// priority solid is unconditionally nearer than any world fragment regardless of
// world extent. Mirrors IRRender::kDepthForegroundBandWidth (ir_render_types.hpp)
// and the GLSL twin (ir_iso_common.glsl).
constant int kDepthForegroundBandWidth = 16384;

// Per-trixel priority tiers (#1960) — twin of ir_iso_common.glsl. Subdivide the
// reserved foreground band into N-1 disjoint equal-width tiers; tier 0 = world
// (out of band). MORE-negative = higher priority (tier N-1 at the near band edge).
// trixel_to_framebuffer selects tier = max(perEntityTier, perTrixelTier). Default
// tier 0 ⇒ byte-identical. Mirror IRRender::kDepthForegroundTier* + the GLSL twin.
constant int kDepthForegroundTierCount = 3;
constant int kDepthForegroundTierWidth = kDepthForegroundBandWidth / (kDepthForegroundTierCount - 1);
inline int depthForegroundTierLo(int kMin, int tier) {
    return kMin + (kDepthForegroundTierCount - 1 - tier) * kDepthForegroundTierWidth;
}
inline int depthForegroundTierHi(int kMin, int tier) {
    return depthForegroundTierLo(kMin, tier) + kDepthForegroundTierWidth - 1;
}
inline int depthForegroundTierCenter(int kMin, int tier) {
    return depthForegroundTierLo(kMin, tier) + kDepthForegroundTierWidth / 2;
}

// Per-trixel priority carrier (#1960) — twin of ir_iso_common.glsl. The per-trixel
// tier rides the top K=2 bits of the 64-bit entity id stored in triangleEntityIds
// (uint2: .x = low word, .y = high word; carrier = bits 30..31 of the high word).
// THE chokepoint: every reader masks via decodeEntityId, the stage-2 writer packs
// via encodeEntityIdWithPriority. Priority 0 ⇒ id unchanged.
constant uint kEntityIdPriorityShiftInHighWord = 30u;
constant uint kEntityIdPriorityMaskInHighWord = 0x3u << kEntityIdPriorityShiftInHighWord;
// Fog cut-face carrier (#2124 lit-cross-section follow-up) — GLSL twin's
// kEntityIdCutFaceMaskInHighWord. Bit 29 flags a fog cross-section CUT face so
// LIGHTING_TO_TRIXEL forces it fully lit (no self-shadow / crease AO). Rides the
// same masking chokepoint; default (non-cut) ⇒ id unchanged (byte-identical).
constant uint kEntityIdCutFaceMaskInHighWord = 0x1u << 29u;
constant uint kEntityIdHighWordMask =
    ~(kEntityIdPriorityMaskInHighWord | kEntityIdCutFaceMaskInHighWord);
inline uint decodePriority(uint2 rawId) {
    return (rawId.y >> kEntityIdPriorityShiftInHighWord) & 0x3u;
}
inline bool decodeCutFace(uint2 rawId) {
    return (rawId.y & kEntityIdCutFaceMaskInHighWord) != 0u;
}
inline uint2 decodeEntityId(uint2 rawId) {
    return uint2(rawId.x, rawId.y & kEntityIdHighWordMask);
}
inline uint2 encodeEntityIdWithPriority(uint2 id, uint priority) {
    return uint2(id.x, (id.y & kEntityIdHighWordMask) |
                           ((priority & 0x3u) << kEntityIdPriorityShiftInHighWord));
}
// Set the fog cut-face flag on an ALREADY priority-encoded id — GLSL twin.
inline uint2 encodeEntityIdCutFace(uint2 packed, bool isCutFace) {
    return isCutFace ? uint2(packed.x, packed.y | kEntityIdCutFaceMaskInHighWord)
                     : packed;
}

// Per-axis fractional encoding (#1458): (depth << 10) | (uFrac4 << 6) | (vFrac4 << 2) | slot
// uFrac4/vFrac4 in 0..15 where 8 = cell centre (fracInCell=0). atomicMin orders by depth first.
// Per-axis canvases clear to INT_MAX (0x7FFFFFFF) so any valid encoding overwrites the sentinel.
// rawDepth must be in world units; depth field is 22 bits so rawDepth must stay < 2^21.
inline int encodeDepthWithFaceFrac(int rawDepth, int slot, int uFrac4, int vFrac4) {
    return (rawDepth << 10) | (uFrac4 << 6) | (vFrac4 << 2) | slot;
}

// Maps fracInCell to 4-bit sub-cell offsets (0..15, 8 = cell centre) for the
// given axis, following the uv assignment of faceInPlaneUnitAxes.
inline void fracToFrac4(int axis, float3 fracInCell, thread int& uFrac4, thread int& vFrac4) {
    if (axis == 0) {
        uFrac4 = clamp(int(fracInCell.y * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.z * 16.0) + 8, 0, 15);
    } else if (axis == 1) {
        uFrac4 = clamp(int(fracInCell.x * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.z * 16.0) + 8, 0, 15);
    } else {
        uFrac4 = clamp(int(fracInCell.x * 16.0) + 8, 0, 15);
        vFrac4 = clamp(int(fracInCell.y * 16.0) + 8, 0, 15);
    }
}

// Convenience overload: compute uFrac4/vFrac4 from fracInCell and encode in one call.
inline int encodeDepthWithFaceFrac(int rawDepth, int slot, int axis, float3 fracInCell) {
    int uFrac4, vFrac4;
    fracToFrac4(axis, fracInCell, uFrac4, vFrac4);
    return encodeDepthWithFaceFrac(rawDepth, slot, uFrac4, vFrac4);
}

// Outward unit normal for the visible side of each iso-rendered face. The
// iso projection has view direction (1,1,1), so a camera at
// (-large,-large,-large) sees the faces whose outward normals point
// AGAINST the view direction — -X, -Y, -Z (+Z is down, so -Z is up = the
// top face). Used by AO sampling (step out of the surface) and lighting
// lambert (dot with sun direction); both consumers MUST share this so
// they agree on "out". GLSL mirror lives in ir_iso_common.glsl.
inline float3 faceOutwardNormal(int face) {
    if (face == kXFace) return float3(-1.0, 0.0, 0.0);
    if (face == kYFace) return float3(0.0, -1.0, 0.0);
    return float3(0.0, 0.0, -1.0);
}

inline int3 faceOutwardNormalI(int face) {
    if (face == kXFace) return int3(-1, 0, 0);
    if (face == kYFace) return int3(0, -1, 0);
    return int3(0, 0, -1);
}

// Six-face polarity-aware outward unit normal. Mirrors
// `faceOutwardNormal6` in shaders/ir_iso_common.glsl and
// `IRMath::faceOutwardNormal(FaceId)`. Used by AO + lighting after the
// per-slot `faceId = visibleFaceIds[slot]` lookup.
inline float3 faceOutwardNormal6(int faceId) {
    if (faceId == kFaceXNeg) return float3(-1.0, 0.0, 0.0);
    if (faceId == kFaceXPos) return float3( 1.0, 0.0, 0.0);
    if (faceId == kFaceYNeg) return float3(0.0, -1.0, 0.0);
    if (faceId == kFaceYPos) return float3(0.0,  1.0, 0.0);
    if (faceId == kFaceZNeg) return float3(0.0, 0.0, -1.0);
    return float3(0.0, 0.0, 1.0);  // kFaceZPos
}

inline int3 faceOutwardNormal6I(int faceId) {
    if (faceId == kFaceXNeg) return int3(-1, 0, 0);
    if (faceId == kFaceXPos) return int3( 1, 0, 0);
    if (faceId == kFaceYNeg) return int3(0, -1, 0);
    if (faceId == kFaceYPos) return int3(0,  1, 0);
    if (faceId == kFaceZNeg) return int3(0, 0, -1);
    return int3(0, 0, 1);  // kFaceZPos
}

// True when face `faceId` is exposed (neighbor cell empty). Encoding
// mirrors `IRComponents::VoxelFlags::kFaceOccluded*`: bit `(2 + faceId)`
// set ⟺ neighbor exists ⟺ face occluded ⟺ skip emit.
inline bool faceIsExposed(uint flagsByte, int faceId) {
    return ((flagsByte >> uint(2 + faceId)) & 1u) == 0u;
}

inline int3 faceMicroPositionFixed(
    int face,
    int3 voxelPositionFixed,
    int u,
    int v
) {
    if (face == kXFace) {
        return int3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (face == kYFace) {
        return int3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    return int3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z
    );
}

// Six-face polarity-aware micro position. For POS faces the fixed-axis
// coordinate sits at `voxelPositionFixed.<axis> + subdivisions`; for NEG
// faces it sits at `voxelPositionFixed.<axis>` (identical to the 3-face
// overload above). Mirrors `faceMicroPositionFixed6` in
// shaders/ir_iso_common.glsl. Used by the subdivided emit path in
// `c_voxel_to_trixel_stage_{1,2}.metal` after the per-slot world
// `faceId = visibleFaceIds[slot]` lookup (#1278).
inline int3 faceMicroPositionFixed6(
    int faceId,
    int3 voxelPositionFixed,
    int u,
    int v,
    int subdivisions
) {
    if (faceId == kFaceXNeg) {
        return int3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceXPos) {
        return int3(
            voxelPositionFixed.x + subdivisions,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceYNeg) {
        return int3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceYPos) {
        return int3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y + subdivisions,
            voxelPositionFixed.z + v
        );
    }
    if (faceId == kFaceZNeg) {
        return int3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y + v,
            voxelPositionFixed.z
        );
    }
    // kFaceZPos
    return int3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z + subdivisions
    );
}

inline bool isInsideCanvas(int2 pixel, int2 canvasSize) {
    return pixel.x >= 0 && pixel.x < canvasSize.x &&
           pixel.y >= 0 && pixel.y < canvasSize.y;
}

inline float3 snapNearIntegerVoxelPosition(float3 voxelPosition) {
    float3 voxelRounded = round(voxelPosition);
    bool3 nearGrid = abs(voxelPosition - voxelRounded) <= float3(0.0001);
    return select(voxelPosition, voxelRounded, nearGrid);
}

// Round-half-up: rounds to the nearest integer, ties go UP. Mirrors
// `IRMath::roundHalfUp` (engine/math/include/irreden/ir_math.hpp) so any
// CPU↔GPU coordinate handshake (occupancy grid build, ray-march cell sampling)
// resolves half-integer voxel positions to the same cell on both sides.
// Hardware `round()` is implementation-defined at half-integers and cannot be
// trusted for that handshake.
inline int3 roundHalfUp(float3 v) {
    return int3(floor(v + float3(0.5)));
}

inline int roundHalfUp(float v) {
    return int(floor(v + 0.5f));
}

inline int2 roundHalfUp(float2 v) {
    return int2(floor(v + float2(0.5)));
}

// Per-voxel iso occlusion depth of model position `pos` projected onto a
// (possibly entity-rotated) iso depth `axis` — the SO(3) generalization of
// pos3DtoDistance (identical to it when axis == (1,1,1)). For a rotated
// DETACHED canvas `axis` is `R⁻¹·(1,1,1)` (uploaded in
// FrameDataVoxelToTrixel.voxelDepthAxis); the world canvas keeps (1,1,1).
// CPU twin: IRMath::isoDepthAlongAxis — roundHalfUp keeps the half-integer
// rounding bit-identical across the CPU/GPU boundary (#1462).
inline int isoDepthAlongAxis(int3 pos, float3 axis) {
    return roundHalfUp(dot(float3(pos), axis));
}

inline int2 trixelOriginOffsetX1(int2 trixelCanvasSize) {
    return trixelCanvasSize / int2(2);
}

inline int2 trixelOriginOffsetZ1(int2 trixelCanvasSize) {
    return trixelOriginOffsetX1(trixelCanvasSize) + int2(-1, -1);
}

inline int trixelOriginModifier(int2 trixelCanvasOffsetZ1, float2 frameCanvasOffset) {
    const float2 canvasOffsetFloored = floor(frameCanvasOffset);
    return (
        trixelCanvasOffsetZ1.x + trixelCanvasOffsetZ1.y +
        int(canvasOffsetFloored.x) + int(canvasOffsetFloored.y)
    ) & 1;
}

// Clamp a float canvas-pixel position into a valid `texture.read()` index.
// Metal's `texture.read()` has no built-in edge handling, unlike GLSL's
// `textureLod()` (implicit `clamp_to_edge` via sampler). (This edge clamp is
// separate from the #442 parity-shift asymmetry — that's answered on
// `trixelFramebufferSamplePosition` below.)
inline uint2 trixelCanvasReadCoord(float2 origin, float2 textureSize) {
    return uint2(clamp(origin, float2(0.0f), textureSize - float2(1.0f)));
}

// Mirror of `trixelFramebufferSamplePosition` in `ir_iso_common.glsl` — the
// parity bit + fract sub-pixel test pick which of the iso cell's two trixels
// this fragment maps to, byte-identical to GLSL/CPU `pos2DIsoToTriangleIndex`.
//
// Unlike GLSL, the Metal gather (trixel_to_framebuffer.metal) reads color/depth
// from the RAW (unshifted) origin; only the hover/pick coord is shifted. Metal's
// vertex stage negates clip `position.y` (top-left render-target origin) where
// GL does not (bottom-left framebuffer origin), so its raster already lands the
// raw sample on the correct trixel row — the equivalent of GL's one-row shift,
// applied implicitly by the flipped raster. Both read the correct trixel for
// their own raster convention. See #442;
// docs/design/trixel-parity-shift-442-investigation.md.
inline float2 trixelFramebufferSamplePosition(float2 origin, int originModifier) {
    const float2 originFloored = floor(origin);
    const float2 fractComp = fract(origin);
    const int parity = (int(originFloored.x) + int(originFloored.y) + originModifier) & 1;
    if (parity != 0) {
        if (fractComp.y < fractComp.x) {
            origin.y -= 1.0f;
        }
    } else if (fractComp.y < 1.0f - fractComp.x) {
        origin.y -= 1.0f;
    }
    return origin;
}

inline int effectiveTrixelSubdivisionScale(int2 voxelRenderOptions) {
    return voxelRenderOptions.x != 0 ? max(voxelRenderOptions.y, 1) : 1;
}

inline int2 trixelFrameOffset(
    int2 trixelCanvasOffsetZ1,
    float2 frameCanvasOffset,
    int2 voxelRenderOptions
) {
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    return trixelCanvasOffsetZ1 + int2(floor(frameCanvasOffset * float(scale)));
}

// NOTE (#1944): the per-axis camera-pan anchor is `trixelOriginOffsetZ1(size) +
// int2(floor(frameCanvasOffset))` — whole-iso, NOT density-scaled — and is
// INLINED at each per-axis site, not centralised here, so this shared header
// gains no symbol that would drift the cardinal SDF/voxel fast path. See the
// GLSL twin and system_trixel_to_framebuffer.hpp for the rationale.

inline int2 trixelCanvasPixelToIsoRel(
    int2 pixel,
    int2 trixelCanvasOffsetZ1,
    float2 frameCanvasOffset,
    int2 voxelRenderOptions
) {
    return pixel - trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
}

// Cardinal Z-yaw helpers (T-055). Mirrors shaders/ir_iso_common.glsl.
// Sign convention is documented there; bodies here match line-for-line.

inline int rasterYawCardinalIndex(float rasterYaw) {
    // CPU snaps visualYaw to a multiple of pi/2 (Camera::computeYawSplit) so
    // this index pick is exact at floats that survived the UBO upload. The
    // round() defends against bit-wise drift only; it is not the cardinal-snap
    // policy itself. Negative inputs (yaw=-pi/2 -> q=-1) fold via the (mod 4 +
    // 4) mod 4 clamp.
    constexpr float kHalfPi = 1.5707963267948966f;
    int q = int(round(rasterYaw / kHalfPi));
    return ((q % 4) + 4) % 4;
}

// (cos, sin) of the cardinal angle named by cardinalIndex — exact ±1/0, the
// snapped Z-yaw the GRID rasterizer projects at. Mirrors
// IRMath::cardinalYawCosSin and ir_iso_common.glsl; retires the open-coded
// cardinalCos/cardinalSin tables that callers used to inline.
inline float2 cardinalYawCosSin(int cardinalIndex) {
    if (cardinalIndex == 1) return float2( 0.0,  1.0);
    if (cardinalIndex == 2) return float2(-1.0,  0.0);
    if (cardinalIndex == 3) return float2( 0.0, -1.0);
    return float2(1.0, 0.0);
}

inline int3 rotateCardinalZ(int3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return int3( v.y, -v.x, v.z);   // R_z(-pi/2)
    if (cardinalIndex == 2) return int3(-v.x, -v.y, v.z);   // R_z(+/-pi)
    if (cardinalIndex == 3) return int3(-v.y,  v.x, v.z);   // R_z(+pi/2)
    return v;
}

// Mirror of `cardinalLowerCornerShift` in shaders/ir_iso_common.glsl.
// See that file for the geometric rationale; bodies match line-for-line.
inline int3 cardinalLowerCornerShift(int cardinalIndex) {
    if (cardinalIndex == 1) return int3(0, -1, 0);
    if (cardinalIndex == 2) return int3(-1, -1, 0);
    if (cardinalIndex == 3) return int3(-1, 0, 0);
    return int3(0, 0, 0);
}

inline float3 rotateCardinalZInv(float3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return float3(-v.y,  v.x, v.z); // R_z(+pi/2)
    if (cardinalIndex == 2) return float3(-v.x, -v.y, v.z); // R_z(+/-pi)
    if (cardinalIndex == 3) return float3( v.y, -v.x, v.z); // R_z(-pi/2)
    return v;
}

inline int3 rotateCardinalZInvI(int3 v, int cardinalIndex) {
    if (cardinalIndex == 1) return int3(-v.y,  v.x, v.z);   // R_z(+pi/2)
    if (cardinalIndex == 2) return int3(-v.x, -v.y, v.z);   // R_z(+/-pi)
    if (cardinalIndex == 3) return int3( v.y, -v.x, v.z);   // R_z(-pi/2)
    return v;
}

// Convenience wrapper for T-057 (picking inverse). T-058 (screen-space residual
// pass) was retired by T-323 — residual yaw lives in faceDeform[] (T-293).
// Not consumed by the current T-055 shaders; scaffolded here so consuming tasks
// can reference it from ir_iso_common directly.
inline float3 isoPixelToWorld3D(int isoX, int isoY, float depth, int cardinalIndex) {
    return rotateCardinalZInv(isoPixelToPos3D(isoX, isoY, depth), cardinalIndex);
}

inline float3 trixelCanvasPixelToWorld3D(
    int2 pixel,
    int rawDepth,
    int2 trixelCanvasOffsetZ1,
    float2 frameCanvasOffset,
    int2 voxelRenderOptions,
    int cardinalIndex
) {
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const int2 isoRel =
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (scale > 1) {
        pos3D /= float(scale);
    }
    if (cardinalIndex != 0) {
        // Undo the rasterizer's `cardinalLowerCornerShift` (applied in
        // world units after division by scale) before rotating back to
        // world coordinates. Matches shaders/ir_iso_common.glsl.
        pos3D -= float3(cardinalLowerCornerShift(cardinalIndex));
        pos3D = rotateCardinalZInv(pos3D, cardinalIndex);
    }
    return pos3D;
}

inline float3 trixelCanvasPixelToWorld3D(
    int2 pixel,
    int rawDepth,
    int2 trixelCanvasOffsetZ1,
    float2 frameCanvasOffset,
    int2 voxelRenderOptions,
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
inline float3 rotateYawZInv(float3 v, float yaw) {
    const float c = cos(yaw);
    const float s = sin(yaw);
    return float3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

// Smooth-camera-yaw inverse (#1719) of the #1345 smooth-yaw SDF store: those
// pixels are placed at roundHalfUp(pos3DtoPos2DIsoYawed(world, visualYaw))
// with the VIEW-frame iso depth (#1370), so recover the view-frame point with
// the cardinal-frame solver and rotate back by the full +visualYaw. No
// lower-corner shift — the smooth store never applies one. Identical to
// trixelCanvasPixelToWorld3D at visualYaw == 0 (cos=1/sin=0, cardinal 0 takes
// the same shift-free path), keeping the cardinal fast path byte-identical.
inline float3 trixelCanvasPixelToWorld3DSmoothYaw(
    int2 pixel,
    int rawDepth,
    int2 trixelCanvasOffsetZ1,
    float2 frameCanvasOffset,
    int2 voxelRenderOptions,
    float visualYaw
) {
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const int2 isoRel =
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    float3 viewPos = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (scale > 1) {
        viewPos /= float(scale);
    }
    return rotateYawZInv(viewPos, visualYaw);
}

// Continuous-yaw + per-face deformation math (T-292; consumed by T-293).
// Mirrors shaders/ir_iso_common.glsl and IRMath::pos3DtoPos2DIsoYawed /
// faceDeformationMatrix / deformedTrixelIsoPixel / sqtToMat4 /
// matrixApplyToVoxelGrid in engine/math/include/irreden/ir_math.hpp.

inline float2 pos3DtoPos2DIsoYawed(float3 worldPos, float visualYaw) {
    const float c = cos(visualYaw);
    const float s = sin(visualYaw);
    const float vx = worldPos.x * c + worldPos.y * s;
    const float vy = -worldPos.x * s + worldPos.y * c;
    return float2(-vx + vy, -vx - vy + 2.0f * worldPos.z);
}

// Exact (unquantized) composite depth key for a forward-scattered face —
// mirror of scatterCompositeDepthKey in ir_iso_common.glsl; see that file for
// the full rationale (the rounded key it replaces tied adjacent micro-cells on
// a foreshortened axis and let draw order pick the farther quad on the
// sign-flip side of a bracket — the #1457 wrong-voxel-color bands). Keeps the
// cardinal encodeDepthWithFace scale (x4 + slot) so it co-sorts with the SDF
// smooth-yaw path. Shared by every forward-scatter composite writer.
//
// Continuous-yaw iso depth — mirror of yawedIsoDistance in ir_iso_common.glsl.
// pos3DtoDistance(R_z(-visualYaw) * worldPos) = x(cos-sin) + y(sin+cos) + z, the
// one shared composite depth metric for SDF + per-axis voxels + the detached
// composite (CPU twin IRMath::pos3DtoDistanceYawed) so all three co-sort at
// every yaw; collapses to un-yawed x+y+z at cardinals (byte-identical).
inline float yawedIsoDistance(float3 worldPos, float visualYaw) {
    const float c = cos(visualYaw);
    const float s = sin(visualYaw);
    return worldPos.x * (c - s) + worldPos.y * (s + c) + worldPos.z;
}

inline float scatterCompositeDepthKey(float3 origin, float visualYaw, int slot) {
    return yawedIsoDistance(origin, visualYaw) * 4.0f + float(slot);
}

// Conservative XY growth of an axis-aligned half-extent swept under a Z-yaw of
// (cosYaw, sinYaw): each in-plane axis grows to |c|*hX + |s|*hY, Z unchanged.
// CPU mirror: IRMath::yawGrownIsoHalfExtent; GLSL mirror in ir_iso_common.glsl.
inline float3 yawGrownIsoHalfExtent(float3 halfExtent, float cosYaw, float sinYaw) {
    const float absC = abs(cosYaw);
    const float absS = abs(sinYaw);
    return float3(halfExtent.x * absC + halfExtent.y * absS,
                  halfExtent.x * absS + halfExtent.y * absC,
                  halfExtent.z);
}

// 2x2 deformation matrix per face for residual yaw. At residualYaw == 0 all
// three return identity. `face` uses the kXFace / kYFace / kZFace convention;
// other values return identity. CPU mirror: IRMath::faceDeformationMatrix.
inline float2x2 faceDeformationMatrix(int face, float residualYaw) {
    const float c = cos(residualYaw);
    const float s = sin(residualYaw);
    if (face == kXFace) {
        return float2x2(float2(c - s, 1.0f - (c + s)), float2(0.0f, 1.0f));
    }
    if (face == kYFace) {
        return float2x2(float2(c + s, c - s - 1.0f), float2(0.0f, 1.0f));
    }
    if (face == kZFace) {
        return float2x2(float2(c, -s), float2(s, c));
    }
    return float2x2(float2(1.0f, 0.0f), float2(0.0f, 1.0f));
}

inline int2 deformedTrixelIsoPixel(int face, int subPixel, float residualYaw) {
    const int2 unyawed = faceOffset_2x3(face, subPixel);
    const float2x2 D = faceDeformationMatrix(face, residualYaw);
    const float2 deformed = D * float2(unyawed);
    return int2(roundHalfUp(deformed.x), roundHalfUp(deformed.y));
}

// Rotates vector v by unit quaternion q = (qx, qy, qz, qw).
// CPU mirror: IRMath::rotateVectorByQuat.
inline float3 rotateByQuat(float3 v, float4 q) {
    float3 u = q.xyz;
    float w = q.w;
    float3 t = 2.0f * cross(u, v);
    return v + w * t + cross(u, t);
}

// Rotates vector v by the inverse (conjugate) of unit quaternion q.
inline float3 rotateByInverseQuat(float3 v, float4 q) {
    return rotateByQuat(v, float4(-q.xyz, q.w));
}

// The two in-plane unit model axes (e_u, e_v) a face's scatter quad spans, by
// axis = faceId >> 1 (0=X spans y,z; 1=Y spans x,z; 2=Z spans x,y) — matching
// faceSpanCorner's cornerSel.x -> e_u, cornerSel.y -> e_v ordering. Mirror of
// the GLSL twin in ir_iso_common.glsl.
inline void faceInPlaneUnitAxes(int axis, thread float3& eu, thread float3& ev) {
    eu = (axis == 0) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
    ev = (axis == 2) ? float3(0.0, 1.0, 0.0) : float3(0.0, 0.0, 1.0);
}

// In-plane iso-pixel unit steps (su, sv) for a face's two in-plane world axes —
// the iso directions along which a re-voxelized cell's in-plane neighbour cells
// sit on screen. Mirror of the GLSL twin in ir_iso_common.glsl (#1557): the
// detached re-voxelize raster dilates each surface face's footprint by ±su / ±sv
// so the sub-cell gaps round-to-cell leaves between adjacent rotated cells fill
// with the nearest (occlusion-winning, correct-colour) surface face. Normalised
// to ~1px so the silhouette grows by at most a pixel along the surface, never
// across a concave notch.
inline void faceInPlaneIsoSteps(int faceId, thread int2& su, thread int2& sv) {
    float3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    su = roundHalfUp(normalize(float2(pos3DtoPos2DIso(int3(eu)))));
    sv = roundHalfUp(normalize(float2(pos3DtoPos2DIso(int3(ev)))));
}

// Visit-bound margin (framebuffer pixels) the per-axis forward-scatter grows each
// quad by along each screen edge normal. Originally the conservative-coverage
// margin (#1494); as of the #1937 analytic-coverage rework (Metal-lead) it is
// ONLY a rasterization visit-bound — f_peraxis_scatter now decides coverage
// analytically from the true [0,1]^2 footprint, so this just has to be wide
// enough (~1px) that every fragment the true footprint could touch gets visited.
// The GL twin still carries the old coverage role; it ports to the visit-bound in
// #1938.
constant float kScatterDilateMarginPx = 0.85;

// Depth penalty (x4+slot key scale) a scatter fragment in the conservative-
// dilation MARGIN adds — mirror of kScatterMarginDepthBiasKey in
// ir_iso_common.glsl; see that file for the #1457 rationale (margins only
// fill pixels no exact footprint claims; never beat a same-plane owner).
constant float kScatterMarginDepthBiasKey = 0.25;

// Deterministic cell tiebreak (#2255) — mirror of kScatterCellTieStep /
// kScatterCellTieBand in ir_iso_common.glsl; see that file for the full
// rationale (margin-yield crossover pixels tie bit-exactly between parallel
// neighbor faces and previously fell to the #1961 compaction's run-variant
// draw order; quantize the final fragment depth to the band and inject the
// 8-level cell code into the sub-band bits so ties resolve by cell identity).
constant float kScatterCellTieStep = 1.0f / 8388608.0f;
constant float kScatterCellTieBand = 8.0f / 8388608.0f;

// Margin-yield gradient scale (#1883) — mirror of ir_iso_common.glsl. Scales the
// margin yield by the fragment's own plane-extrapolation excursion (penetration
// past the exact footprint x per-axis depth gradient) so a cell-deep per-axis
// margin yields the shared ridge to the neighbor face's exact footprint (the
// doubled top<->side sliver) while sub-pixel gap-fills still win. Folded into the
// yield-grad varying by the scatter vertex stage.
constant float kScatterMarginYieldGradScale = 3.0;

// Miter limit for the conservative dilation below (#1538). Mirror of the GLSL
// constant in ir_iso_common.glsl.
constant float kScatterMiterLimit = 2.0;

// Pitch-proportional coverage fraction for the DETACHED forward-scatter (#1538).
// Mirror of the GLSL constant in ir_iso_common.glsl — a margin floor set to this
// fraction of the on-screen cell pitch closes the detached seam gap at every
// scale without blobbing small cubes.
constant float kScatterDetachedPitchFraction = 0.5;

// Screen-space visit-bound dilation for the per-axis forward-scatter (#1494,
// #1538, #1937). At off-snap residual poses a per-cell deformed rhombus
// foreshortens toward a sub-pixel-thin sliver that slips between fragment centers
// and drops out under pixel-center rasterization. Grow each quad outward; `su`/
// `sv` are the face in-plane unit axes projected to framebuffer pixels,
// `cornerSign` is sign(position). Returns the clip-space (NDC) offset to add.
//
// #1937 (Metal-lead): the margin is now a FIXED `minMarginPx` per edge — the old
// continuous per-axis growth (0.5*|n|) that DECIDED coverage is retired here, so
// the dilation only guarantees the rasterizer VISITS the fragments the true
// footprint could touch. f_peraxis_scatter decides coverage analytically from
// vQuadParam, which removes the #1883 corner-spike-vs-dashing trade-off at the
// source. The #1538 miter geometry below is kept as the visit-bound shape. GL
// twin (still the old per-axis coverage role) ports in #1938.
//
// MITER, not additive sum (#1538): the naive marginPx*(e1+e2) of the two edge
// normals cancels at a sliver's acute corner (e1,e2 antiparallel -> sum ~0),
// leaving the sharp tip un-grown — those tips line up along the foreshortened
// lattice and leak (lattice-aligned cracks + interior speckle on detached
// cubes). The miter marginPx*(e1+e2)/(1+dot(e1,e2)) moves BOTH edges out by
// marginPx, equals the additive sum at a square corner, and keeps the acute tip
// moving outward; clamp |δ| to kScatterMiterLimit*marginPx so a sliver tip can't
// blow into a blob (the failure mode of just raising marginPx).
inline float2 scatterConservativeDilation(
    float2 su, float2 sv, float2 cornerSign, float minMarginPx, float2 ndcPerPx
) {
    // Outward normal of each edge = the component of the OTHER edge perpendicular
    // to it; |nu|/|nv| are the on-screen perpendicular extents across each edge.
    float2 nu = sv - su * (dot(sv, su) / max(dot(su, su), 1e-8f));
    float2 nv = su - sv * (dot(su, sv) / max(dot(sv, sv), 1e-8f));
    bool hasU = dot(nu, nu) > 1e-10f;
    bool hasV = dot(nv, nv) > 1e-10f;
    if (!hasU && !hasV) return float2(0.0);
    // Fixed visit-bound (#1937, Metal-lead): both edges grow by the same fixed
    // minMarginPx. The continuous per-axis growth (0.5*length(nu)) that used to
    // decide coverage — and forced the #1883 corner-spike-vs-silhouette-dashing
    // mutual exclusion — is gone; f_peraxis_scatter now decides coverage
    // analytically. marginU == marginV reduces the miter solve below to the #1538
    // equal-margin miter.
    const float marginU = minMarginPx;
    const float marginV = minMarginPx;
    float2 e1 = hasU ? cornerSign.y * normalize(nu) : float2(0.0); // e_u edge normal
    float2 e2 = hasV ? cornerSign.x * normalize(nv) : float2(0.0); // e_v edge normal
    if (!hasU) return e2 * marginV * ndcPerPx;
    if (!hasV) return e1 * marginU * ndcPerPx;
    // Miter that moves edge-u out by marginU and edge-v by marginV: solve
    // [e1;e2]·δ = (marginU,marginV). Reduces to the #1538 equal-margin miter when
    // marginU==marginV.
    float det = e1.x * e2.y - e1.y * e2.x;
    if (abs(det) < 1e-4f) {
        return float2(-e1.y, e1.x) * (max(marginU, marginV) * kScatterMiterLimit) * ndcPerPx;
    }
    float2 delta = float2(
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

// Analytic edge-aware coverage for the per-axis forward-scatter (#1937, the epic
// #1933 root fix; Metal-lead, GL twin in #1938). `q` is the fragment's position in
// the face's true [0,1]^2 footprint (the scatter's vQuadParam, with the
// visit-bound dilation landing just outside the unit box); `fw = fwidth(q)`
// converts a footprint-parameter distance to framebuffer pixels. `interior` flags
// the 4 edges — .x = u-low (q.x==0), .y = u-high (q.x==1), .z = v-low (q.y==0),
// .w = v-high (q.y==1) — as 1 = occupied same-plane neighbour (interior: the face
// continues, no silhouette here) or 0 = silhouette (boundary).
//
// Interior edges fill the whole visit-bound region solid (coverage 1), so
// foreshortened same-plane cells bridge the sub-pixel scatter gaps between their
// true footprints — the depth-yield bias in f_peraxis_scatter arbitrates the
// resulting 1px overlap, exactly as the old conservative dilation did, so an exact
// footprint owner still wins and only genuine gaps fill. Boundary edges get exact
// sub-pixel box coverage clamp(0.5 + distPx, 0, 1): at a convex corner two
// boundary edges intersect, so min() yields a crisp corner with no #1883 spike,
// and a foreshortened silhouette gets per-pixel partial coverage instead of
// dropping out (no dashing). Returns min coverage across the 4 edges; the caller
// hard-thresholds it at 0.5 (no alpha blend — the R32I/depth co-sort write is a
// single per-pixel value).
inline float scatterAnalyticEdgeCoverage(float2 q, float2 fw, float4 interior) {
    const float2 inv = 1.0f / max(fw, float2(1e-5f));
    // Signed pixel distance to each edge; + is inside the footprint.
    const float dULo = q.x * inv.x;
    const float dUHi = (1.0f - q.x) * inv.x;
    const float dVLo = q.y * inv.y;
    const float dVHi = (1.0f - q.y) * inv.y;
    const float cULo = (interior.x > 0.5f) ? 1.0f : clamp(0.5f + dULo, 0.0f, 1.0f);
    const float cUHi = (interior.y > 0.5f) ? 1.0f : clamp(0.5f + dUHi, 0.0f, 1.0f);
    const float cVLo = (interior.z > 0.5f) ? 1.0f : clamp(0.5f + dVLo, 0.0f, 1.0f);
    const float cVHi = (interior.w > 0.5f) ? 1.0f : clamp(0.5f + dVHi, 0.0f, 1.0f);
    return min(min(cULo, cUHi), min(cVLo, cVHi));
}

// Builds the local->world matrix from an SQT triple (scale, quaternion
// rotation, translation). Composition is T * R * S; quaternion layout matches
// the engine canon: float4(qx, qy, qz, qw) with .w the scalar; identity is
// (0, 0, 0, 1). CPU mirror: IRMath::sqtToMat4.
inline float4x4 sqtToMat4(float3 scaleVec, float4 rotationQuat, float3 translation) {
    const float x = rotationQuat.x;
    const float y = rotationQuat.y;
    const float z = rotationQuat.z;
    const float w = rotationQuat.w;
    const float3 col0 = float3(1.0f - 2.0f * (y * y + z * z),
                               2.0f * (x * y + w * z),
                               2.0f * (x * z - w * y)) * scaleVec.x;
    const float3 col1 = float3(2.0f * (x * y - w * z),
                               1.0f - 2.0f * (x * x + z * z),
                               2.0f * (y * z + w * x)) * scaleVec.y;
    const float3 col2 = float3(2.0f * (x * z + w * y),
                               2.0f * (y * z - w * x),
                               1.0f - 2.0f * (x * x + y * y)) * scaleVec.z;
    return float4x4(
        float4(col0, 0.0f),
        float4(col1, 0.0f),
        float4(col2, 0.0f),
        float4(translation, 1.0f)
    );
}

inline int3 matrixApplyToVoxelGrid(float4x4 transformMat, int3 cell) {
    const float4 worldPos = transformMat * float4(float3(cell), 1.0f);
    return roundHalfUp(float3(worldPos.x, worldPos.y, worldPos.z));
}

// Frame data layout used by all voxel→trixel compute kernels.  Mirrors the
// FrameDataVoxelToTrixel UBO in the GLSL pipeline.  std140 padding rules from
// GLSL collapse cleanly into Metal's natural packing here.
struct FrameDataVoxelToTrixel {
    float2 frameCanvasOffset;
    int2 trixelCanvasOffsetZ1;
    int2 voxelRenderOptions;
    int2 voxelDispatchGrid;
    int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster (byte-
    // identical); 1/2/3 = the X/Y/Z per-axis canvas pass (#1309).
    int perAxisRoute;
    int2 canvasSizePixels;
    int2 cullIsoMin;
    int2 cullIsoMax;
    float visualYaw;
    float rasterYaw;    // consumed: cardinal-snap basis selection
    // baked into faceDeform[] CPU-side. The pre-T-293 screen-space residual
    // composite (T-058 / T-322) that consumed this as a post-trixel rotation
    // was retired by T-323.
    float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas. Gates
    // emitDeformedFace super-sampling to the detached path only — see
    // c_voxel_to_trixel_stage_1.glsl for the super-sampling contract.
    float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major: .xy = col0, .zw =
    // col1 of `IRMath::faceDeformationMatrix(axis(visibleFaceIds[slot]),
    // residualYaw)`. **Indexed by visible-triplet SLOT (0/1/2)**, not by
    // axis — at non-zero cardinal the world face whose matrix lives at
    // slot s changes per `visibleFaceIds[s]`. Identity when residualYaw==0
    // so cardinal-snap stays bit-identical pixel-for-pixel against pre-#1278
    // master at cardinal 0. Mirrors the GLSL `vec4 faceDeform[3]`.
    float4 faceDeform[3];
    // Per-slot world FaceId (0..5) — the three camera-visible faces
    // resolved on the CPU via `IRMath::visibleFaceTripletCardinal` (#1278).
    // .w is std140 padding. Mirrors `ivec4 visibleFaceIds` in the GLSL
    // UBO declarations.
    int4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). (1,1,1) for the world canvas / identity entity →
    // isoDepthAlongAxis collapses to x+y+z (byte-identical); a rotated
    // DETACHED canvas uploads IRMath::isoDepthAxisModel(rotation). .w is
    // padding. Mirrors `vec4 voxelDepthAxis` appended in the GLSL stage-1 UBO
    // and FrameDataVoxelToCanvas::voxelDepthAxis_. Other kernels that bind
    // this struct never read it.
    float4 voxelDepthAxis;
    // World-receive offset for an opt-in world-placed detached re-voxelize solid
    // (#1576 P4b-2). .xyz = the entity world cell origin
    // (roundVec3HalfUp(translation)); .w = 1.0 when worldPlaced_, else 0.0.
    // c_lighting_to_trixel recovers world pos as modelPos + .xyz and samples the
    // shared world sun-shadow map + light volume there; .w == 0 keeps the default
    // screen-locked path byte-identical. Mirrors
    // FrameDataVoxelToCanvas::detachedWorldReceive_. Also read by
    // c_voxel_to_trixel_stage_{1,2} (#2127) to recover the world column for the fog
    // cut-face cross-section on a world-placed detached re-voxelize canvas.
    float4 detachedWorldReceive;
    // Un-widened (no shadow-feeder sweep) iso cull viewport for the depth-only
    // shadow-feeder path (#1740). .xy = floor(min), .zw = ceil(max). A voxel
    // inside [cullIsoMin, cullIsoMax] but OUTSIDE this box is an off-screen
    // shadow feeder — c_voxel_to_trixel_stage_2 skips its colour/entity-id taps
    // (stage 1 still wrote its full-res depth, all the bake + AO read). Mirrors
    // FrameDataVoxelToCanvas::visibleIsoBounds_. Other kernels never read it.
    int4 visibleIsoBounds;
    // Per-axis deterministic-winner resolve mode (#2255). 0 = the normal
    // distance store. 1 = the winner-resolve dispatch between the stage-1
    // store and stage 2: re-run the identical per-axis geometry and, for each
    // face whose encoded distance matches the settled per-cell atomic-min
    // winner, atomic-min the face's run-stable voxel pool index into the
    // per-cell winner scratch (buffer 28 = kBufferIndex_PerAxisResolveScratch,
    // transiently reused) — so stage 2's color/entity-id tap admits exactly
    // one of the equal-key faces. Mirrors FrameDataVoxelToCanvas::resolveMode_
    // (offset 192); pads mirror the CPU struct's 16-byte-stride tail. Read
    // only by c_voxel_to_trixel_stage_1.
    int resolveMode;
    int _resolveModePad0;
    int _resolveModePad1;
    int _resolveModePad2;
};

// Smooth analytic vision-circle reveal for one fog disc, shared by
// c_fog_to_trixel (per-pixel floor reveal) and c_voxel_to_trixel_stage_1
// (per-voxel object clip) so the floor edge and the voxel-object edge are the
// SAME analytic curve (#2102) — one formula, no CPU/GPU or GL/Metal drift.
// `circle` = (centerX, centerY, radius, edgeSoftness) in world units; `aa` is
// an extra antialias half-width (c_fog_to_trixel passes its per-pixel
// worldPerPixel for a zoom-stable rim; the voxel clip passes 0 for a binary
// inside/outside test — `reveal >= 0.5` is `worldXY inside radius` regardless
// of the softening width, since smoothstep is 0.5 at its midpoint). Returns
// 1.0 fully revealed, 0.0 fully hidden.
inline float fogVisionCircleReveal(float2 worldXY, float4 circle, float aa) {
    const float dist = length(worldXY - circle.xy);
    const float a = max(circle.w, aa);
    return 1.0f - smoothstep(circle.z - a, circle.z + a, dist);
}

#endif // IR_ISO_COMMON_METAL_INCLUDED
