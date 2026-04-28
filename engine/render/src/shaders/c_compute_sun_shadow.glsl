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

// Matches system_build_occupancy_grid.hpp's SSBO sizing.
const int kOccupancyGridSize = 256;
const int kOccupancyGridHalfExtent = 128;

// Must match `kEmptyDistanceEncoded` in c_compute_voxel_ao.glsl.
const int kEmptyDistanceEncoded = 65535;

// Max iso-march steps per pixel. 64 covers the common "building casting
// a shadow" scale while bounding per-frame work — the full pass is
// O(canvasPixels * steps) so tune here if the 1 ms budget slips.
const int kMaxShadowMarchSteps = 64;

// Darkening applied to fully-shadowed pixels. 0.45 leaves enough
// detail visible inside shadows; tweak here rather than in the lighting
// pass so the shadow texture stays the single source of truth.
const float kShadowDarken = 0.45;
const int kMaxAnalyticShapeMarchSteps = 32;
const float kAnalyticShadowSurfaceThreshold = 0.35;

const uint SHAPE_BOX = 0u;
const uint SHAPE_SPHERE = 1u;
const uint SHAPE_CYLINDER = 2u;
const uint SHAPE_ELLIPSOID = 3u;
const uint SHAPE_CURVED_PANEL = 4u;
const uint SHAPE_WEDGE = 5u;
const uint SHAPE_TAPERED_BOX = 6u;
const uint SHAPE_CONE = 8u;
const uint SHAPE_TORUS = 9u;
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

float sdfBox(vec3 p, vec3 halfExtents) {
    vec3 d = abs(p) - halfExtents;
    return max(d.x, max(d.y, d.z));
}

float sdfSphere(vec3 p, float radius) {
    return length(p) - radius;
}

float sdfCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdfEllipsoid(vec3 p, vec3 radii) {
    if (radii.x <= 0.0 || radii.y <= 0.0 || radii.z <= 0.0) return 1.0;
    float k0 = length(p / radii);
    if (k0 < 1e-6) return -min(radii.x, min(radii.y, radii.z));
    float k1 = length(p / (radii * radii));
    return k0 * (k0 - 1.0) / k1;
}

float sdfTaperedBox(vec3 p, vec3 halfExtents, float taper) {
    float taperFactor = mix(
        1.0, taper,
        clamp((p.z + halfExtents.z) / (2.0 * halfExtents.z), 0.0, 1.0)
    );
    vec3 scaled = vec3(p.xy / max(taperFactor, 0.001), p.z);
    return sdfBox(scaled, halfExtents);
}

float sdfCone(vec3 p, float baseRadius, float halfHeight) {
    float t = clamp((p.z + halfHeight) / (2.0 * halfHeight), 0.0, 1.0);
    float radiusAtZ = baseRadius * (1.0 - t);
    float dRadial = length(p.xy) - radiusAtZ;
    float dZ = abs(p.z) - halfHeight;
    float dOutside = length(max(vec2(dRadial, dZ), 0.0));
    float dInside = min(max(dRadial, dZ), 0.0);
    return dOutside + dInside;
}

float sdfTorus(vec3 p, float majorR, float minorR) {
    float q = length(p.xy) - majorR;
    return length(vec2(q, p.z)) - minorR;
}

float sdfWedge(vec3 p, vec3 halfExtents) {
    float boxD = sdfBox(p, halfExtents);
    float planeD = p.z - halfExtents.z * (1.0 - p.x / max(halfExtents.x, 0.001));
    return max(boxD, planeD);
}

float sdfCurvedPanel(vec3 p, vec3 halfExtents, float curvature) {
    float nx = p.x / max(halfExtents.x, 0.001);
    float zMid = curvature * halfExtents.x * nx * nx;
    float dThickness = abs(p.z - zMid) - halfExtents.z;
    float dX = abs(p.x) - halfExtents.x;
    float dY = abs(p.y) - halfExtents.y;
    float dOutside = length(max(vec3(dX, dY, dThickness), 0.0));
    float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

float evaluateShapeSDF(vec3 localPos, uint shapeType, vec4 params) {
    vec3 halfSize = params.xyz * 0.5;
    switch (shapeType) {
        case SHAPE_BOX:          return sdfBox(localPos, halfSize);
        case SHAPE_SPHERE:       return sdfSphere(localPos, params.x);
        case SHAPE_CYLINDER:     return sdfCylinder(localPos, params.x, halfSize.z);
        case SHAPE_ELLIPSOID:    return sdfEllipsoid(localPos, halfSize);
        case SHAPE_TAPERED_BOX:  return sdfTaperedBox(localPos, halfSize, params.w);
        case SHAPE_CONE:         return sdfCone(localPos, params.x, halfSize.z);
        case SHAPE_TORUS:        return sdfTorus(localPos, params.x, params.y);
        case SHAPE_WEDGE:        return sdfWedge(localPos, halfSize);
        case SHAPE_CURVED_PANEL: return sdfCurvedPanel(localPos, halfSize, params.w);
        default:                 return sdfBox(localPos, halfSize);
    }
}

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
    float minStep = 1.0 / float(min(subdivisions, 4));

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
            float distance = evaluateShapeSDF(samplePos - center, shape.shapeType, shape.params);
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

    vec3 rayPos = rayOrigin;
    for (int step = 0; !shadowed && step < kMaxShadowMarchSteps; ++step) {
        ivec3 cell = ivec3(round(rayPos));
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
        rayPos += sunDir;
    }

    float factor = shadowed ? kShadowDarken : 1.0;
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}
