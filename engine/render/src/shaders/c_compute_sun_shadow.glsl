#version 460 core

// Per-pixel directional sun shadow compute. For each rasterized surface
// pixel (voxel OR shape) reconstructs the voxel-space surface position
// from the encoded distance texture, then marches from that position
// toward the sun through the 3D occupancy grid. If any solid voxel is
// hit within kMaxShadowMarchSteps the pixel is shadowed, otherwise lit.
// Result written as a 0..1 brightness factor into the R channel of the
// canvas sun-shadow texture, consumed later by LIGHTING_TO_TRIXEL.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_sdf_common.glsl"

// Matches system_build_occupancy_grid.hpp's SSBO sizing.
const int kOccupancyGridSize = 256;
const int kOccupancyGridHalfExtent = 128;

// Must match `kEmptyDistanceEncoded` in c_compute_voxel_ao.glsl.
const int kEmptyDistanceEncoded = 65535;

// Max occupancy-grid cells visited per pixel by the 3D DDA below. Each
// step advances the ray to the next cell boundary (Amanatides-Woo
// traversal), so this caps total cells touched rather than ray distance.
// 128 covers diagonal rays (which visit ~sqrt(3)× more cells per unit of
// ray length than axis-aligned ones) for the same effective shadow
// distance the old fixed-step march produced. The full pass is
// O(canvasPixels * steps); tune here if the per-frame budget slips.
const int kMaxShadowMarchSteps = 128;
// Hard cap on ray distance in voxel units. Mirrors the old fixed-step
// march (64 unit-length steps = 64 units), so the longest shadow a
// caster can throw is unchanged. Per-cell `tMax` from the DDA is what
// terminates the loop when we exceed this.
const float kMaxShadowRayDistance = 64.0;

// Darkening applied to fully-shadowed pixels. 0.45 leaves enough
// detail visible inside shadows; tweak here rather than in the lighting
// pass so the shadow texture stays the single source of truth.
const float kShadowDarken = 0.45;
const int kMaxAnalyticShapeMarchSteps = 32;
const float kAnalyticShadowSurfaceThreshold = 0.05;

// Shape-type constants (SHAPE_BOX, …) live in ir_sdf_common.glsl.
const uint FLAG_HOLLOW = 1u;

struct ShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

layout(std430, binding = 28) readonly buffer OccupancyGrid {
    uint occupancyBits[];
};

// Mirrors GPUOccupancyEntityBounds in ir_render_types.hpp. Filled by
// system_build_occupancy_grid each frame: one entry per voxel-pool
// entity that contributed at least one in-bounds voxel. The per-pixel
// shader linear-scans this list to find the surface entity's bbox so
// it can skip self-cells during the occupancy march — same role as
// selfEntityId exclusion in the analytic shape path.
struct OccupancyEntityBounds {
    uvec4 entityId;
    ivec4 minCell;
    ivec4 maxCell;
};

layout(std430, binding = 4) readonly buffer OccupancyEntityBoundsBuffer {
    OccupancyEntityBounds occupancyEntityBounds[];
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
};

layout(std140, binding = 29) uniform FrameDataSun {
    // xyz = unit vector pointing from the world toward the sun; w unused.
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int shapeCasterCount;
    uniform int occupancyBoundsCount;
    uniform int _sunPadding0;
    uniform int _sunPadding1;
    uniform int _sunPadding2;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;
layout(rg32ui, binding = 2) readonly uniform uimage2D trixelEntityIds;

layout(std430, binding = 20) readonly buffer SunShadowShapeCasterBuffer {
    ShapeDescriptor shapeCasters[];
};

bool occupancyGetBit(int wx, int wy, int wz) {
    int he = kOccupancyGridHalfExtent;
    if (wx < -he || wx >= he || wy < -he || wy >= he || wz < -he || wz >= he) {
        return false;
    }
    uint x = uint(wx + he);
    uint y = uint(wy + he);
    uint z = uint(wz + he);
    uint flat =
        (z * uint(kOccupancyGridSize) + y) * uint(kOccupancyGridSize) + x;
    uint bits = occupancyBits[flat >> 5u];
    return ((bits >> (flat & 31u)) & 1u) == 1u;
}

// SDF primitives (sdfBox, sdfSphere, …) and `evaluateSDF` live in
// ir_sdf_common.glsl, shared with the shape rasterizer.

float shapeBoundingRadius(ShapeDescriptor shape) {
    vec3 halfSize = abs(shape.params.xyz) * 0.5;
    switch (shape.shapeType) {
        case SHAPE_SPHERE:    return shape.params.x + 0.5;
        case SHAPE_CYLINDER:  return length(vec2(shape.params.x, halfSize.z)) + 0.5;
        case SHAPE_CONE:      return length(vec2(shape.params.x, halfSize.z)) + 0.5;
        case SHAPE_TORUS:     return shape.params.x + shape.params.y + 0.5;
        default:              return length(halfSize) + abs(shape.params.w) + 0.5;
    }
}

bool rayIntersectsSphere(vec3 origin, vec3 dir, vec3 center, float radius,
                         out float tNear, out float tFar) {
    vec3 oc = origin - center;
    float b = dot(oc, dir);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return false;
    h = sqrt(h);
    tNear = -b - h;
    tFar = -b + h;
    return tFar > 0.0;
}

bool analyticShapeShadowHit(vec3 rayOrigin, vec3 rayDir, uint selfEntityId) {
    if (voxelRenderOptions.x == 0 || shapeCasterCount <= 0) return false;

    int subdivisions = max(voxelRenderOptions.y, 1);
    // minStep is the smallest the sphere-trace step is allowed to be
    // (when the SDF distance estimate is small). Setting it to one
    // sub-voxel = `1 / subdivisions` keeps the per-step precision
    // matched to the render resolution: at zoom 16 the march can still
    // resolve to a sub-pixel level. The previous `min(subdivisions, 4)`
    // clamp held minStep at 0.25 voxel even at zoom 16, which produced
    // ~4 px of jaggedness at the shadow boundary because neighboring
    // rays could only converge on a 0.25-voxel grid.
    float minStep = 1.0 / float(subdivisions);

    for (int i = 0; i < shapeCasterCount; ++i) {
        ShapeDescriptor shape = shapeCasters[i];
        if (shape.entityId == selfEntityId) continue;

        vec3 center = shape.worldPosition.xyz;
        float tNear, tFar;
        if (!rayIntersectsSphere(
                rayOrigin, rayDir, center, shapeBoundingRadius(shape), tNear, tFar
            )) {
            continue;
        }

        float t = max(tNear, minStep);
        for (int step = 0; step < kMaxAnalyticShapeMarchSteps && t <= tFar; ++step) {
            vec3 samplePos = rayOrigin + rayDir * t;
            float distance = evaluateSDF(samplePos - center, shape.shapeType, shape.params);
            bool hollow = (shape.flags & FLAG_HOLLOW) != 0u;
            if ((!hollow && distance <= kAnalyticShadowSurfaceThreshold) ||
                (hollow && abs(distance) <= kAnalyticShadowSurfaceThreshold)) {
                return true;
            }
            t += max(distance * 0.75, minStep);
        }
    }
    return false;
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (shadowsEnabled == 0) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    int rawDepth = encoded >> 2;

    // Same reconstruction as c_compute_voxel_ao.glsl — keep the two in
    // lockstep so AO and shadow sample the same voxel cells.
    int subdivisions = max(voxelRenderOptions.y, 1);
    vec2 canvasOffset = (voxelRenderOptions.x != 0)
        ? frameCanvasOffset * float(subdivisions)
        : frameCanvasOffset;
    ivec2 isoRel =
        pixel - trixelCanvasOffsetZ1 - ivec2(floor(canvasOffset));

    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }

    uint selfEntityId = imageLoad(trixelEntityIds, pixel).x;

    // Look up this surface's voxel-pool bbox so the occupancy march below
    // can skip self-cells. Mirrors the analytic path's selfEntityId
    // exclusion. SDF surfaces and pixels where the entity didn't make it
    // into the bounds buffer simply fall through with the sentinel range,
    // matching no cell — i.e. behaving as if no self-exclusion is needed.
    ivec3 selfMin = ivec3(2147483647);
    ivec3 selfMax = ivec3(-2147483648);
    for (int i = 0; i < occupancyBoundsCount; ++i) {
        if (occupancyEntityBounds[i].entityId.x == selfEntityId) {
            selfMin = occupancyEntityBounds[i].minCell.xyz;
            selfMax = occupancyEntityBounds[i].maxCell.xyz;
            break;
        }
    }

    vec3 sunDir = sunDirection.xyz;
    vec3 rayOrigin = pos3D + sunDir;
    bool shadowed = analyticShapeShadowHit(rayOrigin, sunDir, selfEntityId);

    // 3D DDA (Amanatides-Woo) over the occupancy grid. Visits every cell
    // the ray actually crosses, so the shadow boundary is determined by
    // analytic ray-vs-cell-face intersections rather than the discrete
    // 1-unit-step sampling the old march did. Fixed-step sampling could
    // miss cells when the ray clipped a corner and produced multi-pixel
    // zigzag artifacts on the cast shadow boundary; DDA's per-cell
    // visitation eliminates the misses, and the boundary stair-step
    // collapses to the projected cell-face spacing (~one cell on screen).
    //
    // Cells are indexed by `roundHalfUp` so cell `i` covers the spatial
    // range `[i - 0.5, i + 0.5)`. For step direction +1 the next cell
    // boundary is at `i + 0.5`; for -1 it's at `i - 0.5`. `tDelta` is
    // the parametric distance along the ray to traverse one full cell
    // along that axis.
    vec3 rayPos = rayOrigin;
    ivec3 cell = roundHalfUp(rayPos);
    ivec3 stepCell = ivec3(
        sunDir.x > 0.0 ? 1 : (sunDir.x < 0.0 ? -1 : 0),
        sunDir.y > 0.0 ? 1 : (sunDir.y < 0.0 ? -1 : 0),
        sunDir.z > 0.0 ? 1 : (sunDir.z < 0.0 ? -1 : 0)
    );
    // Direction-component clamp to keep tMax / tDelta finite for nearly
    // axis-aligned suns. 1e30 is large enough that the min() comparison
    // below never picks the clamped axis as the next step.
    vec3 invDir = vec3(
        stepCell.x != 0 ? 1.0 / sunDir.x : 1e30,
        stepCell.y != 0 ? 1.0 / sunDir.y : 1e30,
        stepCell.z != 0 ? 1.0 / sunDir.z : 1e30
    );
    vec3 nextEdge = vec3(cell) + 0.5 * vec3(stepCell);
    vec3 tMax = (nextEdge - rayPos) * invDir;
    vec3 tDelta = abs(invDir);
    if (stepCell.x == 0) { tMax.x = 1e30; tDelta.x = 1e30; }
    if (stepCell.y == 0) { tMax.y = 1e30; tDelta.y = 1e30; }
    if (stepCell.z == 0) { tMax.z = 1e30; tDelta.z = 1e30; }

    for (int step = 0; !shadowed && step < kMaxShadowMarchSteps; ++step) {
        int he = kOccupancyGridHalfExtent;
        if (cell.x < -he || cell.x >= he ||
            cell.y < -he || cell.y >= he ||
            cell.z < -he || cell.z >= he) {
            // Left the grid without hitting anything — lit.
            break;
        }
        bool isSelfCell =
            cell.x >= selfMin.x && cell.x <= selfMax.x &&
            cell.y >= selfMin.y && cell.y <= selfMax.y &&
            cell.z >= selfMin.z && cell.z <= selfMax.z;
        if (!isSelfCell && occupancyGetBit(cell.x, cell.y, cell.z)) {
            shadowed = true;
            break;
        }
        // Advance to the next cell along the ray — the axis with the
        // smallest tMax is the one whose boundary the ray reaches first.
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            cell.x += stepCell.x;
            tMax.x += tDelta.x;
        } else if (tMax.y < tMax.z) {
            cell.y += stepCell.y;
            tMax.y += tDelta.y;
        } else {
            cell.z += stepCell.z;
            tMax.z += tDelta.z;
        }
        if (min(min(tMax.x, tMax.y), tMax.z) > kMaxShadowRayDistance) {
            break;
        }
    }

    float factor = shadowed ? kShadowDarken : 1.0;
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}
