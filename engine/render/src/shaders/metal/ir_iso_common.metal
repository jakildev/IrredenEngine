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

// Per-voxel SO(3) visible-triplet unpack from C_Voxel::reserved (buffer 6,
// offset 8). Mirror of `IRComponents::VoxelReservedSO3` /
// `packVoxelVisibleTriplet` in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp and of the GLSL
// helpers in ir_iso_common.glsl — keep all three in lockstep. A MAIN_CANVAS_SO3
// entity (#1299) stamps its octahedral-snapped visible triplet here; bit 0
// marks the voxel as carrying a valid triplet.
inline bool reservedHasSO3(uint reserved) {
    return (reserved & 1u) != 0u;
}

// `slot` is the visible-triplet index 0/1/2 (X/Y/Z axis); returns FaceId 0..5.
inline int unpackReservedFaceId(uint reserved, int slot) {
    return int((reserved >> uint(1 + slot * 3)) & 0x7u);
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
// `textureLod()` (implicit `clamp_to_edge` via sampler). See #442 for the
// broader GLSL/Metal coord-shift reconciliation.
inline uint2 trixelCanvasReadCoord(float2 origin, float2 textureSize) {
    return uint2(clamp(origin, float2(0.0f), textureSize - float2(1.0f)));
}

// Mirror of `trixelFramebufferSamplePosition` in `ir_iso_common.glsl`. The
// parity bit + fract sub-pixel test pick which row of the iso quad cell's
// two trixels this fragment maps to. Identical math to GLSL/CPU
// `pos2DIsoToTriangleIndex` so the same canvas data reads back the same
// way on both backends.
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

// --- Smooth camera Z-yaw forward-scatter: face-local in-plane store (#1310) ---
// Mirror of shaders/ir_iso_common.glsl. See that file for the full rationale:
// each visible face is stored at the dense, collision-free lattice of its two
// in-plane world axes (X -> (y,z), Y -> (x,z), Z -> (x,y)), replacing the
// iso-position store that dropped compressed-axis faces (cracks) and whose
// recovery was singular at yaw = +/-120 deg (speckle). axis = faceId >> 1.

inline int2 faceInPlaneCoords(int faceId, int3 worldPos) {
    const int axis = faceId >> 1;
    if (axis == 0) return int2(worldPos.y, worldPos.z);
    if (axis == 1) return int2(worldPos.x, worldPos.z);
    return int2(worldPos.x, worldPos.y);
}

// Inverse of faceInPlaneCoords: exact integer recovery (rawDepth = x + y + z).
inline int3 faceOriginFromInPlane(int faceId, int2 inPlane, int rawDepth) {
    const int third = rawDepth - inPlane.x - inPlane.y;
    const int axis = faceId >> 1;
    if (axis == 0) return int3(third, inPlane.x, inPlane.y);
    if (axis == 1) return int3(inPlane.x, third, inPlane.y);
    return int3(inPlane.x, inPlane.y, third);
}

// Camera-tracking anchor (canvas-native units) centering the store on screen;
// uses the non-singular UN-yawed iso inverse. Store + scatter must compute it
// identically (matching perAxisBase + canvasSize) — they do.
inline int3 faceLocalAnchor(int2 perAxisBase, int2 canvasSize) {
    const int2 isoCenter = canvasSize / int2(2) - perAxisBase;
    return roundHalfUp(isoPixelToPos3D(isoCenter.x, isoCenter.y, 0.0f));
}

// Face-local storage base for `axis`: cell = canvasSize/2 + inPlane(origin - anchor).
inline int2 faceLocalBase(int axis, int3 anchor, int2 canvasSize) {
    int2 anchorInPlane;
    if (axis == 0) anchorInPlane = int2(anchor.y, anchor.z);
    else if (axis == 1) anchorInPlane = int2(anchor.x, anchor.z);
    else anchorInPlane = int2(anchor.x, anchor.y);
    return canvasSize / int2(2) - anchorInPlane;
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
};

#endif // IR_ISO_COMMON_METAL_INCLUDED
