// Shared isometric math utilities for all trixel pipeline Metal compute
// shaders.  Mirrors shaders/ir_iso_common.glsl.  Resolved by the engine's
// Metal #include preprocessor at runtime, NOT by the standard preprocessor —
// header guards are still recommended for safety.
#ifndef IR_ISO_COMMON_METAL_INCLUDED
#define IR_ISO_COMMON_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

constant int kXFace = 0;
constant int kYFace = 1;
constant int kZFace = 2;

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

// Convenience wrapper for T-057 (picking inverse) and T-058 (screen-space residual pass).
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
    float rasterYaw
) {
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const int2 isoRel =
        trixelCanvasPixelToIsoRel(pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
    float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (scale > 1) {
        pos3D /= float(scale);
    }
    if (cardinalIndex != 0) {
        pos3D = rotateCardinalZInv(pos3D, cardinalIndex);
    }
    return pos3D;
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
    int voxelDispatchPadding;
    int2 canvasSizePixels;
    int2 cullIsoMin;
    int2 cullIsoMax;
    float visualYaw;    // not consumed in T-055 — scaffolded for T-058
    float rasterYaw;    // consumed: cardinal-snap basis selection
    float residualYaw;  // not consumed in T-055 — scaffolded for T-058
    float _yawPadding;  // not consumed in T-055 — scaffolded for T-058
};

#endif // IR_ISO_COMMON_METAL_INCLUDED
