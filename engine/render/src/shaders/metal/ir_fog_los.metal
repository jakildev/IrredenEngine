// Fog line-of-sight gate — Metal twin of ../ir_fog_los.glsl (keep the pure
// helpers byte-identical; FogCrossSectionShaderParity compares them). The
// kernel passes the column-field texture as an argument, so no binding macro
// is needed here. Every function is `static`, so a single include per wrapper
// needs no self-guard.

// Mirrors kFogLosCellsPerUnit / kFogLosFieldHalfExtent / kFogLosTextureSize /
// kFogLosLevelCount / kFogLosLevelBias / kFogLosColumnEmpty /
// kFogLosClearanceTolerance (component_canvas_fog_of_war.hpp) and
// kFogLosDiscMargin / kFogLosRimFadeCells / kFogLosMaxMarchSteps /
// kFogLosMarchNudge (fog_line_of_sight.hpp).
constant int kFogLosCellsPerUnit = 2;
constant int kFogLosFieldHalfExtent = 256;
constant int kFogLosTextureSize = 256;
constant int kFogLosLevelCount = 8;
constant int kFogLosLevelBias = 65536;
constant float kFogLosColumnEmpty = 3.402823466e+38;
constant float kFogLosClearanceTolerance = 0.001;
constant float kFogLosDiscMargin = 2.0;
constant float kFogLosRimFadeCells = 8.0;
constant float kFogLosFaceSnapOffset = 0.4;
constant float kFogLosFaceBias = 0.02;
constant int kFogLosMaxMarchSteps = 4096;
constant float kFogLosMarchNudge = 0.004;

constant int kFogLosRouteCardinal = 0;
constant int kFogLosRoutePerAxis = 1;
constant int kFogLosRouteAnalytic = 2;

static float4 fogLosTexel(int2 texel, texture2d<float, access::read> fogLineOfSight) {
    return fogLineOfSight.read(uint2(texel));
}

static bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

static int fogLosLevelRowOffset(int level) {
    return 2 * kFogLosTextureSize - ((2 * kFogLosTextureSize) >> level);
}

static int2 fogLosBlockMin(int level, int2 halfCell) {
    return (((halfCell + int2(kFogLosLevelBias)) >> level) << level) - int2(kFogLosLevelBias);
}

static int2 fogLosFieldMin(int2 windowOrigin, int windowEdge) {
    const int2 corner = (windowOrigin + int2(windowEdge / 2)) * kFogLosCellsPerUnit -
        int2(kFogLosFieldHalfExtent);
    return fogLosBlockMin(kFogLosLevelCount - 1, corner);
}

static bool fogLosCellInField(int2 halfCell, int2 fieldMin) {
    const int2 local = halfCell - fieldMin;
    return local.x >= 0 && local.x < 2 * kFogLosFieldHalfExtent && local.y >= 0 &&
        local.y < 2 * kFogLosFieldHalfExtent;
}

static float fogLosBlockTop(
    int level, int2 blockMin, int2 fieldMin, texture2d<float, access::read> fogLineOfSight
) {
    if (!fogLosCellInField(blockMin, fieldMin)) {
        return kFogLosColumnEmpty;
    }
    const int2 block = (blockMin - fieldMin) >> level;
    const int2 texel = int2(block.x >> 1, fogLosLevelRowOffset(level) + (block.y >> 1));
    return fogLosTexel(texel, fogLineOfSight)[(block.x & 1) + 2 * (block.y & 1)];
}

static float fogLosTopPlane(
    int2 halfCell, int2 fieldMin, texture2d<float, access::read> fogLineOfSight
) {
    return fogLosBlockTop(0, halfCell, fieldMin, fogLineOfSight);
}

static float fogLosReach(float4 circle) {
    const float edge = max(circle.w, 0.0);
    return circle.z + edge + (edge == 0.0 ? kFogLosRimFadeCells : 0.0) + kFogLosDiscMargin;
}

static float3 fogLosEye(float4 circle, float observerZ, float eyeHeight) {
    return float3(circle.xy, observerZ - eyeHeight);
}

static float fogLosTraceClearance(
    float3 eye,
    float3 target,
    float softness,
    int2 fieldMin,
    thread float &bandClearance,
    texture2d<float, access::read> fogLineOfSight
) {
    const float kCells = float(kFogLosCellsPerUnit);
    const float kNever = kFogLosColumnEmpty;
    const int kMaxLevel = kFogLosLevelCount - 1;
    const float2 start = eye.xy * kCells;
    const float2 delta = target.xy * kCells - start;
    const int2 step = int2(sign(delta));
    const float2 nudge = float2(step) * kFogLosMarchNudge;
    // The eye's own half-cell is the one holding its position; the walk starts
    // in the cell ahead of it, which is the same cell off a lattice line.
    const int2 eyeCell = int2(floor(start));
    const float rise = target.z - eye.z;
    const float bandHorizon = target.z - kFogLosClearanceTolerance;
    float minClearance = kNever;
    bandClearance = kNever;
    float t = 0.0;
    int2 cell = int2(floor(start + nudge));
    int level = 0;
    for (int i = 0; i < kFogLosMaxMarchSteps; ++i) {
        const float size = float(1 << level);
        const int2 blockMin = fogLosBlockMin(level, cell);
        const float2 exitEdge =
            float2(blockMin) + float2(step.x > 0 ? size : 0.0, step.y > 0 ? size : 0.0);
        const float2 tEdge = float2(
            step.x == 0 ? kNever : (exitEdge.x - start.x) / delta.x,
            step.y == 0 ? kNever : (exitEdge.y - start.y) / delta.y
        );
        const float tExit = min(min(tEdge.x, tEdge.y), 1.0);
        const float top = (level == 0 && cell.x == eyeCell.x && cell.y == eyeCell.y)
            ? kNever
            : fogLosBlockTop(level, blockMin, fieldMin, fogLineOfSight);
        if (top != kNever) {
            const float tLow = rise > 0.0 ? tExit : t;
            const float clearance = top - (eye.z + tLow * rise);
            const bool inBand = top < bandHorizon;
            if (level == 0) {
                minClearance = min(minClearance, clearance);
                if (inBand) {
                    bandClearance = min(bandClearance, clearance);
                }
                if (clearance < -kFogLosClearanceTolerance) {
                    return minClearance;
                }
            } else {
                const float needed = (inBand && softness > 0.0)
                    ? min(bandClearance, softness)
                    : -kFogLosClearanceTolerance;
                if (clearance < needed) {
                    --level;
                    continue;
                }
            }
        }
        if (tExit >= 1.0) {
            break;
        }
        t = tExit;
        const float2 next = start + delta * t + nudge;
        cell = int2(floor(next));
        // Climb while the crossing lies on a coarser block's boundary: only
        // then is the block ahead one the walk has not already found blocking.
        const bool crossedX = tEdge.x <= tEdge.y;
        const bool crossedY = tEdge.y <= tEdge.x;
        const int edgeX = int(exitEdge.x) + kFogLosLevelBias;
        const int edgeY = int(exitEdge.y) + kFogLosLevelBias;
        for (; level < kMaxLevel; ++level) {
            const int mask = (2 << level) - 1;
            if (!((crossedX && (edgeX & mask) == 0) || (crossedY && (edgeY & mask) == 0))) {
                break;
            }
        }
    }
    return minClearance;
}

static float fogLosVisibilityFromClearance(float minClearance, float bandClearance, float softness) {
    if (minClearance < -kFogLosClearanceTolerance) {
        return 0.0;
    }
    if (softness <= 0.0) {
        return 1.0;
    }
    return smoothstep(0.0, softness, bandClearance);
}

static float fogLosVisibility(
    float3 eye,
    float3 target,
    float softness,
    int2 fieldMin,
    texture2d<float, access::read> fogLineOfSight
) {
    float bandClearance;
    const float minClearance =
        fogLosTraceClearance(eye, target, softness, fieldMin, bandClearance, fogLineOfSight);
    return fogLosVisibilityFromClearance(minClearance, bandClearance, softness);
}

static float3 fogLosCanonicalSample(float3 pos3D, int worldFaceId, int route, int scale) {
    if (route == kFogLosRouteAnalytic) {
        return pos3D;
    }
    const float3 normal = faceOutwardNormal6(worldFaceId);
    float3 canonical = pos3D;
    if (route == kFogLosRouteCardinal) {
        const float3 axisMask = abs(normal);
        const float outward = dot(normal, float3(1.0));
        const float micro = float(scale);
        const float coordinate = dot(pos3D, axisMask);
        const float snapped =
            float(roundHalfUp((coordinate - outward * kFogLosFaceSnapOffset / micro) * micro)) / micro;
        canonical = mix(pos3D, float3(snapped), axisMask);
    }
    return canonical + normal * kFogLosFaceBias;
}
