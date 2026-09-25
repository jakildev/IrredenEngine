/*
 * Project: Irreden Engine
 * File: f_trixel_to_framebuffer.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */


#include "ir_iso_common.glsl"
#if IR_SHAPE_RECEIVER
#include "ir_sdf_common.glsl"
#include "ir_shape_data.glsl"
#include "ir_shape_receiver.glsl"
#include "ir_selected_shape_receiver.glsl"
#include "ir_sun_shadow_sample.glsl"
#if IR_SHAPE_LIGHTING
#include "ir_shape_surface_lighting.glsl"
#endif
#endif

in vec2 TexCoords;

layout (binding = 0) uniform sampler2D triangleColors;
layout (binding = 1) uniform isampler2D  triangleDistances;
layout (binding = 2) uniform usampler2D triangleEntityIds;

layout(std140, binding = 1) uniform GlobalConstants {
    uniform int kMinTriangleDistance;
    uniform int kMaxTriangleDistance;
};

layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    // Shared scatter/gather layout; the surface diagnostic uses the slot at
    // byte 160 as the camera-to-world quaternion for view-aligned caster faces.
    ivec2 perAxisBase;
    float visualYaw;
    int scatterDebugMode;
    ivec4 visibleFaceIds;
    vec4 _detachedResidualPad;
    vec4 casterViewToWorld;
    vec4 scatterFbResolution;
    int depthColorMode;
    float depthColorExtent;
    // Offset 200. 0 = no per-trixel-priority voxel in this canvas, so the tier
    // read of triangleEntityIds is skipped; != 0 = read + decode the tier.
    int anyPerTrixelPriority;
    // Composite depth tier for this draw: 0 = world content (clamped out of the
    // reserved near band), != 0 = foreground priority (pinned into it).
    int depthPriorityMode;
    int overflowMode;
    int trixelSampleLayout;
};

layout(std430, binding = 14) buffer HoveredEntityIdBuffer {
    uvec2 hoveredEntityId;
    float hoveredDepth;
};

out vec4 FragColor;

float normalizeDistance(int dist) {
    return float(dist - kMinTriangleDistance) / float(kMaxTriangleDistance - kMinTriangleDistance);
}

void main() {
    ivec2 textureSize = textureSize(triangleColors, 0);
    ivec2 z1 = trixelOriginOffsetZ1(textureSize);
    // Rectangular color/depth/tier/id reads and the hover compare all use the
    // raw interpolated canvas position; local display triangles instead
    // select cells in the private canvas basis. Nothing here maps onto the
    // triangle lattice (trixelFramebufferSamplePosition): hover identity
    // follows display identity — docs/design/trixel-parity-shift-442-investigation.md.
    vec2 originRaw = TexCoords * vec2(textureSize);

    vec2 displayOrigin = originRaw;
    if (trixelSampleLayout == 1) {
        displayOrigin = localTrixelFramebufferSamplePosition(originRaw, z1);
        if (any(lessThan(displayOrigin, vec2(0.0))) ||
            any(greaterThanEqual(displayOrigin, vec2(textureSize)))) discard;
    }
    vec4 color = textureLod(triangleColors, displayOrigin / textureSize, 0);
    int rawDist = textureLod(triangleDistances, displayOrigin / textureSize, 0).r;
#if IR_SHAPE_RECEIVER && !IR_SHAPE_LIGHTING
    if (color.a >= 0.1) {
        vec3 position = vec3(0.0), normal = vec3(0.0);
        if (selectedShapeBoxReceiver(clamp(ivec2(floor(displayOrigin)), ivec2(0), textureSize - 1), textureSize.x,
                                     originRaw, position, normal)) {
            float visibility = shadowsEnabled == 0 ? 1.0 :
                worldShapeSurfaceSunShadowFactor(position, normal, shapeCanvasIsoDepth(position, receiverFrame),
                    casterViewToWorld);
            color.rgb = visibility >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0);
        }
    }
#endif


#if IR_SHAPE_LIGHTING
    if (color.a >= 0.1 && lightingEnabled != 0) {
        const ivec2 ownerPixel = clamp(ivec2(floor(displayOrigin)), ivec2(0), textureSize - 1);
        vec3 position = vec3(0.0), normal = vec3(0.0);
        if (selectedShapeBoxReceiver(ownerPixel, textureSize.x, originRaw, position, normal))
            color.rgb = shapeSurfaceLighting(ownerPixel, textureSize.x, position, normal,
                shapeCanvasIsoDepth(position, receiverFrame), casterViewToWorld, color.rgb);
    }
#endif

    // effectiveSubdivisionsForHover.y carries the per-canvas depth rescale
    // (effSub / cubeSub) for world-placed DETACHED canvases: their model-frame
    // rawDist was written at the canvas's own (possibly capped) subdivision, so
    // it must be lifted into the shared framebuffer depth units
    // (worldDepth × effSub × 8) before the world iso-depth offset is added.
    // 0 (world/overlay canvases, zero-init) → 1.0, i.e. no rescale.
    float depthScale = effectiveSubdivisionsForHover.y;
    if (depthScale <= 0.0) depthScale = 1.0;
    // roundHalfUp, not hardware round(): a fractional depthScale (effSub /
    // cubeSub) lands odd rawDist values on exact .5 ties, where GLSL round()
    // is implementation-defined and diverges from the Metal twin.
    int base = roundHalfUp(float(rawDist) * depthScale);
    // Match voxel-to-trixel write: texture coord = trixelOriginOffsetZ1 + canvasOffset + worldIndex
    // canvasOffset is already scaled by subdivisions in smooth mode (CPU side)
    // mouseHoveredTriangleIndex is base space; scale to subdivided space for comparison
    int subdivisions = max(int(effectiveSubdivisionsForHover.x), 1);
    vec2 hoveredPosition =
        mouseHoveredTriangleIndex * float(subdivisions) +
        vec2(trixelOriginOffsetZ1(textureSize)) +
        canvasOffset;
    // Hovered = the fragments that display the cursor's raw texel (CPU
    // mouseCanvasTexelWorld). They all read the same texel below, so the
    // non-atomic SSBO write is value-identical across writers.
    ivec2 originIndex = ivec2(floor(displayOrigin));
    ivec2 hoveredIndex = ivec2(floor(hoveredPosition));
    bool isMouseHovered = all(equal(hoveredIndex, originIndex));
    // Priority and color/depth must select the same stored cell. Canvases
    // without prioritized voxels avoid the extra entity-id fetch.
    int tier = depthPriorityMode;
    if (anyPerTrixelPriority != 0) {
        uvec2 sampleEntityId = textureLod(triangleEntityIds, displayOrigin / vec2(textureSize), 0).rg;
        // Resolve the tier: the higher of this draw's per-entity tier
        // (depthPriorityMode, C_EntityCanvas::depthPriority_) and the
        // per-voxel tier authored into the id carrier.
        tier = max(depthPriorityMode, int(decodePriority(sampleEntityId)));
    }
    int foregroundCeil = kMinTriangleDistance + kDepthForegroundBandWidth;
    int enc;
    if (tier == 0) {
        // World content: clamp OUT of the reserved near band. A no-op for every
        // in-budget fragment (base + distanceOffset >> foregroundCeil); far world
        // content saturates against the boundary rather than letting a background
        // fragment beat a priority solid.
        enc = max(base + distanceOffset, foregroundCeil + 1);
    } else {
        // Foreground tier: center this fragment's model-frame local iso-depth in
        // the resolved tier and pin it into the tier's disjoint sub-range. A
        // higher tier is a strictly more-negative (nearer) sub-range, so it wins
        // unconditionally against every lower tier and all world content,
        // independent of world extent. A pathologically deep solid saturates
        // against its tier edge (graceful degradation) instead of escaping. The
        // per-draw distanceOffset (world placement) is intentionally dropped here
        // — priority OVERRIDES world depth ordering.
        enc = clamp(base + depthForegroundTierCenter(kMinTriangleDistance, tier),
                    depthForegroundTierLo(kMinTriangleDistance, tier),
                    depthForegroundTierHi(kMinTriangleDistance, tier));
    }
    float depth = normalizeDistance(enc);
    if (isMouseHovered) {
        if (color.a >= 0.1 && depth <= hoveredDepth) {
            // Strip the per-trixel priority carrier so a prioritized fragment
            // reports its true picked id. Read at the same texel the color and
            // depth gate above sampled: the stage-2 writers store color and id
            // together, so this is the id of what the fragment displays.
            uvec2 entityId = decodeEntityId(
                textureLod(triangleEntityIds, displayOrigin / vec2(textureSize), 0).rg);
            if (entityId != uvec2(0u)) {
                hoveredEntityId = entityId;
                hoveredDepth = depth;
            }
        }
        if (showHoverHighlight > 0.0) {
            color = vec4(1.0, 0.0, 0.0, 1.0);
            depth = 0.0;
        }
    }
    if(color.a < 0.1) {
		discard;
	}
	FragColor = color;
    gl_FragDepth = depth;
}
