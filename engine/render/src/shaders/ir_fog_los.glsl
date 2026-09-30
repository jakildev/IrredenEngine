// Fog line-of-sight gate — the shader half of the column field
// C_CanvasFogOfWar builds on the CPU (component_canvas_fog_of_war.hpp states
// the model). The field is a 256 × 510 RGBA32F image: level 0 holds one
// integer cell per texel with its four half-cells in the four channels
// (channel `(hx & 1) + 2 * (hy & 1)`), each value the column's top plane (+Z
// is down; kFogLosColumnEmpty = no occluder), and the field's pyramid follows
// from each level's texel row (fogLosLevelRowOffset): per block of 2^level
// half-cells on a side, the highest top plane in the block, packed the same
// way. The field is anchored with the fog window: its lower-corner half-cell
// (fogLosFieldMin) derives from the observer block's window origin and the fog
// texture's edge exactly as FogLosColumnField::fieldMinForWindow does. A
// sample's visibility is the exact segment march from a source's eye to the
// sample's canonical position over that lattice, stepping over the pyramid
// blocks that cannot change its verdict. Out-of-field columns are empty.
//
// Include-FRAGMENT: the wrapper supplies the image slot before including it:
//   #define IR_FOG_LOS_BINDING <slot>
// c_fog_to_trixel binds image 4; the GL probe test binds its own slot. The
// kernel includes ir_iso_common first.
// Metal twin: metal/ir_fog_los.metal — keep the pure helpers byte-identical
// (FogCrossSectionShaderParity compares them); only fogLosTexel reads the
// image in each dialect's own spelling.

// Mirrors kFogLosCellsPerUnit / kFogLosFieldHalfExtent / kFogLosTextureSize /
// kFogLosLevelCount / kFogLosLevelBias / kFogLosColumnEmpty /
// kFogLosClearanceTolerance (component_canvas_fog_of_war.hpp) and
// kFogLosDiscMargin / kFogLosRimFadeCells / kFogLosMaxMarchSteps /
// kFogLosMarchNudge (fog_line_of_sight.hpp).
const int kFogLosCellsPerUnit = 2;
const int kFogLosFieldHalfExtent = 256;
const int kFogLosTextureSize = 256;
const int kFogLosLevelCount = 8;
const int kFogLosLevelBias = 65536;
const float kFogLosColumnEmpty = 3.402823466e+38;
const float kFogLosClearanceTolerance = 0.001;
const float kFogLosDiscMargin = 2.0;
const float kFogLosRimFadeCells = 8.0;
// The cardinal voxel raster recovers a face's pixels between zero and one
// micro cell outside the face plane; stepping back this fraction of a micro
// cell before rounding lands every one of them on the plane.
const float kFogLosFaceSnapOffset = 0.4;
// A sample steps this far out of its face, so a vertical face never sits
// inside its own column and a top face rests a hair above its own plane.
const float kFogLosFaceBias = 0.02;
// A level-0 cell of a segment inside the field costs the walk at most one
// climb and one descent around it.
const int kFogLosMaxMarchSteps = 4096;
// How far past a lattice line the walk's position is taken, in half-cells: a
// position on the line belongs to the cell ahead, beyond float rounding.
const float kFogLosMarchNudge = 0.004;

// Sample routes for fogLosCanonicalSample.
const int kFogLosRouteCardinal = 0;  // the cardinal voxel raster's lower-corner lattice
const int kFogLosRoutePerAxis = 1;   // the per-axis / overflow store's lower-corner lattice
const int kFogLosRouteAnalytic = 2;  // an exact world surface point

layout(rgba32f, binding = IR_FOG_LOS_BINDING) readonly uniform image2D fogLineOfSight;

vec4 fogLosTexel(ivec2 texel) {
    return imageLoad(fogLineOfSight, texel);
}

bool fogLosSourceGated(int losSourceMask, int source) {
    return ((losSourceMask >> source) & 1) != 0;
}

// The first packed texel row of pyramid level `level`.
int fogLosLevelRowOffset(int level) {
    return 2 * kFogLosTextureSize - ((2 * kFogLosTextureSize) >> level);
}

// The lower corner of the level-`level` block holding half-cell `halfCell`.
ivec2 fogLosBlockMin(int level, ivec2 halfCell) {
    return (((halfCell + ivec2(kFogLosLevelBias)) >> level) << level) - ivec2(kFogLosLevelBias);
}

// The field's lower-corner half-cell for the fog window at `windowOrigin` of
// edge `windowEdge`: the window's centre less the field's half extent, snapped
// down to a coarsest-level block so every block keeps its world-aligned
// corner. Mirrors FogLosColumnField::fieldMinForWindow.
ivec2 fogLosFieldMin(ivec2 windowOrigin, int windowEdge) {
    const ivec2 corner = (windowOrigin + ivec2(windowEdge / 2)) * kFogLosCellsPerUnit -
        ivec2(kFogLosFieldHalfExtent);
    return fogLosBlockMin(kFogLosLevelCount - 1, corner);
}

bool fogLosCellInField(ivec2 halfCell, ivec2 fieldMin) {
    const ivec2 local = halfCell - fieldMin;
    return local.x >= 0 && local.x < 2 * kFogLosFieldHalfExtent && local.y >= 0 &&
        local.y < 2 * kFogLosFieldHalfExtent;
}

// The highest top plane (the smallest Z) among the half-cells of the
// level-`level` block with lower corner `blockMin` of the field at
// `fieldMin`; empty outside the field. A block is inside or outside the field
// whole.
float fogLosBlockTop(int level, ivec2 blockMin, ivec2 fieldMin) {
    if (!fogLosCellInField(blockMin, fieldMin)) {
        return kFogLosColumnEmpty;
    }
    const ivec2 block = (blockMin - fieldMin) >> level;
    const ivec2 texel = ivec2(block.x >> 1, fogLosLevelRowOffset(level) + (block.y >> 1));
    return fogLosTexel(texel)[(block.x & 1) + 2 * (block.y & 1)];
}

// The top plane of a half-cell; empty outside the field.
float fogLosTopPlane(ivec2 halfCell, ivec2 fieldMin) {
    return fogLosBlockTop(0, halfCell, fieldMin);
}

// Mirrors IRPrefab::Fog::losReach: past this distance from the disc's centre
// a sample is not gated.
float fogLosReach(vec4 circle) {
    const float edge = max(circle.w, 0.0);
    return circle.z + edge + (edge == 0.0 ? kFogLosRimFadeCells : 0.0) + kFogLosDiscMargin;
}

vec3 fogLosEye(vec4 circle, float observerZ, float eyeHeight) {
    return vec3(circle.xy, observerZ - eyeHeight);
}

// Mirrors IRPrefab::Fog::traceLosClearance step for step: the smallest
// clearance the segment keeps above any column it crosses (positive = above;
// kFogLosColumnEmpty when it crosses none), the eye's own half-cell never
// counting. `bandClearance` takes only columns whose top lies strictly above
// the target, the value `softness` grades, so a sample resting on its own
// surface is never softened by that surface. The walk is hierarchical: a
// pyramid block whose highest top, against the segment's lowest point over
// it, cannot lower the verdict (below the tolerance, or below
// min(bandClearance, softness) for a block holding band columns under a soft
// gate) is stepped over whole and the walk climbs to the coarsest level whose
// block ahead is new; a block that might is entered a level finer, and a
// level-0 cell is evaluated exactly. So each result is exact below its
// threshold and otherwise at least it.
float fogLosTraceClearance(
    vec3 eye, vec3 target, float softness, ivec2 fieldMin, out float bandClearance
) {
    const float kCells = float(kFogLosCellsPerUnit);
    const float kNever = kFogLosColumnEmpty;
    const int kMaxLevel = kFogLosLevelCount - 1;
    const vec2 start = eye.xy * kCells;
    const vec2 delta = target.xy * kCells - start;
    const ivec2 step = ivec2(sign(delta));
    const vec2 nudge = vec2(step) * kFogLosMarchNudge;
    // The eye's own half-cell is the one holding its position; the walk starts
    // in the cell ahead of it, which is the same cell off a lattice line.
    const ivec2 eyeCell = ivec2(floor(start));
    const float rise = target.z - eye.z;
    const float bandHorizon = target.z - kFogLosClearanceTolerance;
    float minClearance = kNever;
    bandClearance = kNever;
    float t = 0.0;
    ivec2 cell = ivec2(floor(start + nudge));
    int level = 0;
    for (int i = 0; i < kFogLosMaxMarchSteps; ++i) {
        const float size = float(1 << level);
        const ivec2 blockMin = fogLosBlockMin(level, cell);
        const vec2 exitEdge =
            vec2(blockMin) + vec2(step.x > 0 ? size : 0.0, step.y > 0 ? size : 0.0);
        const vec2 tEdge = vec2(
            step.x == 0 ? kNever : (exitEdge.x - start.x) / delta.x,
            step.y == 0 ? kNever : (exitEdge.y - start.y) / delta.y
        );
        const float tExit = min(min(tEdge.x, tEdge.y), 1.0);
        const float top = (level == 0 && cell.x == eyeCell.x && cell.y == eyeCell.y)
            ? kNever
            : fogLosBlockTop(level, blockMin, fieldMin);
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
        const vec2 next = start + delta * t + nudge;
        cell = ivec2(floor(next));
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

// Mirrors IRPrefab::Fog::losVisibilityFromClearance.
float fogLosVisibilityFromClearance(float minClearance, float bandClearance, float softness) {
    if (minClearance < -kFogLosClearanceTolerance) {
        return 0.0;
    }
    if (softness <= 0.0) {
        return 1.0;
    }
    return smoothstep(0.0, softness, bandClearance);
}

float fogLosVisibility(vec3 eye, vec3 target, float softness, ivec2 fieldMin) {
    float bandClearance;
    const float minClearance = fogLosTraceClearance(eye, target, softness, fieldMin, bandClearance);
    return fogLosVisibilityFromClearance(minClearance, bandClearance, softness);
}

// The canonical position of a pixel: the world point of its face on the
// lattice the column field is built on. A cardinal voxel pixel is recovered
// up to a micro cell off its face plane, so its face-axis coordinate snaps
// onto the micro lattice; a per-axis cell sits exactly on its plane, and an
// analytic pixel is normalized to its authored surface by the caller. Voxel
// routes then step kFogLosFaceBias out along the face normal; an analytic
// sample stays on the authored surface, matching the shape-column stamp.
vec3 fogLosCanonicalSample(vec3 pos3D, int worldFaceId, int route, int scale) {
    if (route == kFogLosRouteAnalytic) {
        return pos3D;
    }
    const vec3 normal = faceOutwardNormal6(worldFaceId);
    vec3 canonical = pos3D;
    if (route == kFogLosRouteCardinal) {
        const vec3 axisMask = abs(normal);
        const float outward = dot(normal, vec3(1.0));
        const float micro = float(scale);
        const float coordinate = dot(pos3D, axisMask);
        const float snapped =
            float(roundHalfUp((coordinate - outward * kFogLosFaceSnapOffset / micro) * micro)) / micro;
        canonical = mix(pos3D, vec3(snapped), axisMask);
    }
    return canonical + normal * kFogLosFaceBias;
}
