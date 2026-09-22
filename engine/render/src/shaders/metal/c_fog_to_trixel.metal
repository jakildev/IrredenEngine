#include "ir_iso_common.metal"
#include "ir_fog_los.metal"

// Mirrors shaders/c_fog_to_trixel.glsl. The grid reveal is a NEAREST read
// (coarse, voxel-quantized memory); the vision-circle reveal is tested against
// the CONTINUOUS world column per pixel, so a disc edge is crisp at render
// resolution and slides smoothly with sub-voxel observer motion.

constant int kFogOfWarSize = 256;
constant int kFogOfWarHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
// Normalized stored explored value (128/255, NOT 0.5). The two-segment lerp
// pivots through this so the three canonical stored states (0 / 128 / 255) land
// exactly on the unexplored / explored / source anchors.
constant float kFogExploredValue = 128.0f / 255.0f;

// Live analytic vision circles. Mirrors kMaxFogVisionCircles and
// FrameDataFogObservers in component_canvas_fog_of_war.hpp — this struct is the
// system's `FogObserverData` upload verbatim. visionCircles[i] =
// (centerX, centerY, radius, edgeSoftness) in world units; only the first
// visionCircleCount entries are read.
constant int kMaxFogVisionCircles = 8;
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles];
    int visionCircleCount;
    // Bit i set = source i is gated by the line-of-sight field (ir_fog_los).
    int losSourceMask;
    // Per-circle height penalty, appended after the count (float4 re-aligns to
    // 16). heights[i] = (observerZ, zCostUp, zCostDown, freeBand); the reveal
    // folds zCostUp * max(dzUp - freeBand, 0) + zCostDown *
    // max(dzDown - freeBand, 0) into the radial distance, where
    // dzUp = max(observerZ - z, 0) and dzDown = max(z - observerZ, 0). All-zero
    // (the default) → the plain 2D disc.
    float4 visionCircleHeights[kMaxFogVisionCircles];
    // Colour of fully unexplored matter — the lerp's state-0 anchor. Only this
    // pass declares it; every other declaration of the struct stops earlier.
    float4 unexploredColor;
};

// Cross-section cap tuning — mirrors the GLSL twin. kFogCutTone is a pure
// multiply with no constant lift, so hidden matter is never brighter than its
// lit self; the cap band is RADIAL surface-XY distance past the rim, applied to
// vertical faces only.
constant float kFogCutTone = 0.85f;
constant float kFogCutMaxRimCells = 2.0f;
// Rim fade — mirrors the GLSL twin. kFogRimFadeCells MUST equal
// kFogHiddenKeepCells (ir_voxel_face_select.metal) so the fade reaches the
// unexplored colour exactly where hidden columns stop rasterizing.
constant float kFogRimFadeCells = 8.0f;
constant float kFogRimFadeLevel = 0.75f;

// Out-of-range cells read as visible (1.0): texture reads have no sampler wrap
// mode, so this bounds check is load-bearing. Matches the OOB-as-visible
// contract on C_CanvasFogOfWar.
static float fogTap(int2 cell, int2 fogSize, texture2d<float, access::read> fog) {
    if (cell.x < 0 || cell.x >= fogSize.x ||
        cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    return fog.read(uint2(cell)).r;
}

kernel void c_fog_to_trixel(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    // Read only for the fog whole-body carrier bit (decodeFogWholeBody).
    texture2d<uint, access::read> triangleCanvasEntityIds [[texture(3)]],
    // Line-of-sight horizons (ir_fog_los). Slot 4 is lighting's sun-shadow
    // input too; lighting rebinds it inside its own tick.
    texture2d<float, access::read> fogLineOfSight [[texture(4)]],
    // buffer(27) ALIASES kBufferIndex_FrameDataLightingToTrixel — the Metal
    // 0-30 buffer table is full, and fog runs right after lighting (done with
    // slot 27 by then). Must match kBufferIndex_FogObservers in
    // ir_render_types.hpp.
    constant FogObserverData& fogObservers [[buffer(27)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }

    // R(-rasterYaw) recovers world coords from the cardinal-rotated raster
    // frame; the fog grid is world-space (X-Y plane).
    const int rawDepth = decodeDepthSingle(encoded);
    float3 pos3D = trixelCanvasPixelToWorld3D(
        pixel,
        rawDepth,
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );

    // Iso convention: X-Y is the floor plane, so the fog grid lookup is (x, y)
    // with the half-extent offset; +Z is the downward height axis and plays no
    // part.
    const int3 surfaceVoxel = roundHalfUp(pos3D);
    const int2 fogCell = surfaceVoxel.xy + int2(kFogOfWarHalfExtent);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    // Kept separate from the circle-combined `state`: the rim fade and the
    // cross-section cap apply only to UNEXPLORED surfaces (explored memory
    // keeps its desaturated tone).
    const float gridState = fogTap(fogCell, fogSize, canvasFogOfWar);
    float state = gridState;
    // This column's world distance PAST the nearest hard disc's radius —
    // drives the cross-section cap band and the rim fade. Initialized to the
    // full fade width so no-hard-disc scenes (including soft Mode B discs)
    // resolve to cap-off + fade 0.
    float hardDistPastRim = kFogRimFadeCells;

    if (fogObservers.visionCircleCount > 0) {
        // Local world-units-per-pixel from the iso inverse-projection Jacobian:
        // recover the +x neighbour at the same depth and measure the world step.
        // Zoom- and iso-correct, so the AA rim stays ~1px at any zoom with no
        // uniform.
        const float3 pos3DNeighborX = trixelCanvasPixelToWorld3D(
            pixel + int2(1, 0),
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
        const float worldPerPixel = length(pos3DNeighborX.xy - pos3D.xy);
        // A whole-body fog-governed body fogs on XY distance alone — mirror of
        // the GLSL twin: both height-penalty terms drop for its pixels.
        const bool fogWholeBody =
            decodeFogWholeBody(triangleCanvasEntityIds.read(uint2(pixel)).xy);
        // Must trace the same analytic curve as c_voxel_to_trixel_stage_1's
        // per-voxel clip, so the floor's per-pixel reveal here and the
        // voxel-object edge there coincide. worldPerPixel floors the rim at ~1
        // canvas px for zoom-stable AA.
        for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
            // Line-of-sight gate — mirror of the GLSL twin: an occluding source
            // contributes neither reveal nor rim distance; whole-body pixels
            // are never gated per pixel.
            if (fogLosSourceGated(fogObservers.losSourceMask, i) && !fogWholeBody &&
                !fogLosVisible(surfaceVoxel, i, fogLineOfSight)) {
                continue;
            }
            // Height-penalized reveal — mirror of the GLSL twin: fold this
            // pixel's world-Z penalty (zCostUp * max(dzUp - freeBand, 0) +
            // zCostDown * max(dzDown - freeBand, 0)) into the radial distance
            // so matter far above/below the observer's height reveals less at
            // the same XY, asymmetrically and with a free band around the
            // observer's height. All-zero heights (or a whole-body pixel) →
            // distEff == the plain 2D length.
            const float4 heights = fogObservers.visionCircleHeights[i];
            const float zCostUp = fogWholeBody ? 0.0f : heights.y;
            const float zCostDown = fogWholeBody ? 0.0f : heights.z;
            const float dzUp = max(heights.x - pos3D.z, 0.0f);
            const float dzDown = max(pos3D.z - heights.x, 0.0f);
            const float distEff = length(pos3D.xy - fogObservers.visionCircles[i].xy) +
                zCostUp * max(dzUp - heights.w, 0.0f) +
                zCostDown * max(dzDown - heights.w, 0.0f);
            const float aa = max(fogObservers.visionCircles[i].w, worldPerPixel);
            const float reveal = 1.0f - smoothstep(
                fogObservers.visionCircles[i].z - aa,
                fogObservers.visionCircles[i].z + aa,
                distEff
            );
            state = max(state, reveal);
            if (fogObservers.visionCircles[i].w == 0.0f) {
                // The rim fade tracks the SAME penalized boundary so a z-hidden
                // surface fades on the z-aware curve, not the 2D one.
                hardDistPastRim =
                    min(hardDistPastRim, distEff - fogObservers.visionCircles[i].z);
            }
        }
    }

    if (state >= 1.0f) {
        return;
    }

    const float4 src = trixelColors.read(uint2(pixel));

    // Cross-section cap — mirrors the GLSL twin: VERTICAL faces (the depth
    // encoding's slot bits resolved through visibleFaceIds) blend the cut tint
    // over the fade with a weight that is 1 at the rim and 0 at
    // kFogCutMaxRimCells, so the wall is one continuous curve converging to the
    // top face's exact fade tone at the band's end — no hard band-edge line, no
    // voxel-stepped teeth. TOP (Z) faces never cap (flat ground fades on the
    // plain radial curve). Colour-only, after lighting — never touches
    // trixelDistances.

    const float luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
    const float3 exploredColor = float3(luminance) * 0.4f;

    // Two-segment continuous lerp anchored on the three canonical stored
    // states: unexploredColor at 0, exploredColor at 128/255, src at 1.0. Alpha
    // is preserved so any text/overlay antialiasing still composites cleanly.
    float3 outColor;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0f - kFogExploredValue);
        outColor = mix(exploredColor, src.rgb, t);
    } else {
        const float t = state / kFogExploredValue;
        outColor = mix(fogObservers.unexploredColor.rgb, exploredColor, t);
    }
    if (gridState < kFogExploredValue) {
        // The squared ease-out crushes the fade tail to the unexplored colour
        // well before the keep-ring drop, so the outermost kept columns' wall
        // faces (whose constant-depth recovery reads a column slightly INSIDE
        // their true one) can't catch a visible lift against the void behind
        // them.
        const float u = 1.0f - smoothstep(0.0f, kFogRimFadeCells, hardDistPastRim);
        outColor = mix(outColor, src.rgb, kFogRimFadeLevel * u * u);
        // `state` carries the disc's ~1px AA rim, so the junction with visible
        // matter stays antialiased. Axis only — the riser-polarity flip never
        // changes a face's axis, so it is not decoded here.
        const int faceAxis = frameData.visibleFaceIds[decodeSlot(encoded)] >> 1;
        if (fogObservers.visionCircleCount > 0 && faceAxis != 2) {
            const float capBlend =
                1.0f - smoothstep(0.0f, kFogCutMaxRimCells, max(hardDistPastRim, 0.0f));
            const float3 capColor = mix(src.rgb * kFogCutTone, src.rgb, state);
            outColor = mix(outColor, capColor, capBlend);
        }
    }
    trixelColors.write(float4(outColor, src.a), uint2(pixel));
}
