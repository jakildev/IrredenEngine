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

// Map local invocation ID within a (2, 3, 1) workgroup to a face type.
// (0,0),(1,0) -> Z_FACE; (1,1),(1,2) -> X_FACE; (0,1),(0,2) -> Y_FACE
int localIDToFace_2x3() {
    if (gl_LocalInvocationID.y == 0) return kZFace;
    if (gl_LocalInvocationID.x == 1) return kXFace;
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

// Outward unit normal for the visible side of each iso-rendered face. With
// +Z = down (gravity), the three visible faces in iso view are +X, +Y, -Z.
// Used by both the AO compute (to step OUTside the surface and read
// neighbor occluders) and the lighting compute (to dot with the
// sun-direction for lambert). Both shaders MUST share this so the surface
// shading and the AO sampling agree on which way is "out".
vec3 faceOutwardNormal(int face) {
    if (face == kXFace) return vec3(1.0, 0.0, 0.0);
    if (face == kYFace) return vec3(0.0, 1.0, 0.0);
    return vec3(0.0, 0.0, -1.0);
}

ivec3 faceOutwardNormalI(int face) {
    if (face == kXFace) return ivec3(1, 0, 0);
    if (face == kYFace) return ivec3(0, 1, 0);
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
