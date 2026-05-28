// Shared isometric math utilities for all trixel pipeline compute shaders.
// Included via #include "ir_iso_common.glsl" (resolved by the engine's
// shader preprocessor at compile time).

const int kXFace = 0;
const int kYFace = 1;
const int kZFace = 2;

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

// Encode depth with face priority for deterministic depth-test resolution.
// The *4 spacing ensures face indices never cross depth boundaries.
int encodeDepthWithFace(int rawDepth, int face) {
    return rawDepth * 4 + face;
}

// Outward unit normal for the visible side of each iso-rendered face. The
// iso projection has view direction (1,1,1), so the three faces a camera
// at (-large, -large, -large) actually sees are the ones whose outward
// normals point AGAINST the view direction — i.e. -X, -Y, -Z (+Z is down,
// so -Z is up = the top face). Used by both the AO compute (to step OUT
// of the surface and read neighbor occluders) and the lighting lambert
// (dot with sun direction). Both consumers MUST share this so AO sampling
// and shading agree on which way is "out".
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
// Adding this shift keeps the diamond 2x3 emit aligned with the rotated
// voxel's iso footprint at every cardinal (iso-pixel granularity; the
// lane-to-world-face mapping remains cardinal-dependent — see AO/lighting
// shaders). At cardinal 0 the shift is zero so the cardinal-snap path
// stays bit-identical to master.
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
