#version 450 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO, directional sun
// shadow, flood-fill light volume) and modulates the canvas color in
// place.
//
// When hdrEnabled is set, the pass computes in unclamped float precision,
// adds the sky-term contribution, applies exposure, and tonemaps via the
// ACES Filmic curve before writing back to the RGBA8 canvas. The HDR
// dynamic range lives entirely in shader-local variables; no canvas
// format change is needed for v1.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// Shared caster/receiver sun-space projection (#2083); must precede
// ir_sun_shadow_sample.glsl (the include resolver is non-recursive).
#include "ir_sun_projection.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), and worldSunShadowFactor() — for
// the opt-in detached re-voxelize world-receive path (#1576 P4b-2). Shared with
// c_compute_sun_shadow; replaces this pass's former local FrameDataSun block.
#include "ir_sun_shadow_sample.glsl"

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    uniform int   lightingEnabled;
    uniform int   lutEnabled;
    uniform int   lightVolumeEnabled;
    uniform float debugLightLevel;
    uniform int   debugOverlayMode;
    uniform int   hdrEnabled;
    uniform float exposure;
    uniform float skyIntensity;
    uniform vec4  skyColor;
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = lighting
    // a per-axis canvas (#1311), reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // 1.0 for a detached entity canvas (re-voxelize solid), 0.0 for the world
    // canvas. A detached re-voxelize canvas carries no sun-shadow map / light
    // volume (#1558) — the branch below forces shadow = 1.0 and disables the
    // light-volume term so slots 4/5 (inert placeholders) are never sampled.
    uniform float isDetachedCanvas;
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5) — see c_voxel_to_trixel_stage_1.glsl + #1278.
    // Lighting maps the decoded depth slot → world FaceId for the
    // six-face outward normal used by Lambert + the HDR sky-term.
    uniform ivec4 visibleFaceIds;
    uniform vec4 _voxelDepthAxisUnused;   // voxelDepthAxis_ in the full UBO (unused here)
    // World-receive offset (#1576 P4b-2). `.xyz` = the opt-in world-placed
    // detached re-voxelize entity's world cell origin; `.w` = 1.0 when the solid
    // opts into world placement, else 0.0. Recovers each detached voxel's world
    // pos as (model pos + .xyz) for the shared sun-shadow + light-volume sample.
    uniform vec4 detachedWorldReceive;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasAO;
layout(binding = 3) uniform sampler2D paletteLUT;
// canvasSunShadow sits at image unit 4. The Metal backend flattens
// texture_ and imageTexture_ tables into a shared setTexture slot space,
// so it cannot collide with paletteLUT at unit 3 or lightVolume at unit
// 5 — keep the unit numbers in lockstep across GLSL and MSL.
layout(rgba8, binding = 4) readonly uniform image2D canvasSunShadow;
layout(binding = 5) uniform sampler3D lightVolume;
// Entity-id channel (#2124 lit-cross-section follow-up): read ONLY to recover the
// fog cut-face flag (bit 29, set by c_voxel_to_trixel_stage_2 via decodeCutFace).
// Bound at image unit 6 on the single-canvas + detached routes; the per-axis
// rotation route leaves it unbound and the `perAxisRoute == 0` guard below skips
// the read. Non-fog scenes never set the flag ⇒ byte-identical.
layout(rg32ui, binding = 6) readonly uniform uimage2D trixelEntityIds;
// Winning-light ID volume (#2318, L2), image unit 7. `.r` = the index+1 (÷255)
// of the light that won each cell's flood contest. Fetched (NEAREST — no
// interpolation) only when the frame gathered a SPOT light
// (`lightVolumeWorldOrigin.w != 0`), so no-spot scenes never read it and stay
// byte-identical. Bound every tick so Metal's slot table is populated.
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;

// SPOT cone shaping (#2318). The winning-light ID indexes this list to recover
// the light's cone axis (`directionAndRadius.xyz`), aperture
// (`coneAndSeedAlpha.x`, full degrees), and TRUE apex (`trueOriginVoxel.xyz`).
// Bound transiently at slot 4 by LIGHTING_TO_TRIXEL; only read on the spot path.
struct GPULightSource {
    vec4 originAndType;
    vec4 colorAndIntensity;
    vec4 directionAndRadius;
    vec4 coneAndSeedAlpha;
    vec4 trueOriginVoxel;
};
layout(std430, binding = 4) readonly buffer LightSourceBuffer {
    GPULightSource lights[];
};

// Per-axis empty-cell compaction (#2256): on the per-axis route (perAxisRoute !=
// 0) this kernel is dispatched indirectly over only each axis's OCCUPIED cells
// (compacted by the STAGE_1 per-axis pre-pass) instead of sweeping the full
// worst-case grid. compactedCells holds the occupied linear cell indices;
// cellDrawArgs carries visibleCount at [kDispatchArgsBaseUint + 3] for the
// in-shader 1-D bound guard. Unused on the single-canvas / detached route
// (perAxisRoute == 0), which keeps the 2-D gl_GlobalInvocationID path.
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

// Phase 1c (#360): the light volume is camera-anchored. The CPU
// uploads `lightVolumeWorldOrigin` (the world voxel that maps to the
// volume's center texel) each frame; subtract it from `pos3D` before
// converting to a sample coordinate. Mirrors LightVolumeParams in
// ir_render_types.hpp — `.xyz` is the volume origin, `.w` is the
// has-SPOT flag (#2318).
// Layout tombstones — must match the propagate/seed UBO layout
// (c_seed_light_volume.glsl, c_propagate_light_volume.glsl). Lighting
// only reads the origin; leading-underscore names mark the unused slots.
layout(std140, binding = 23) uniform LightVolumeParams {
    int _gridSize;
    int _halfExtent;
    int _lightCount;
    float _stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
// The volume covers world voxels in [-half, half) with one texel per
// voxel; sample coords are `(worldVoxel + half + 0.5) / size` to land
// at texel centers.
const float kLightVolumeSize = 128.0;
const float kLightVolumeHalfExtent = 64.0;

// SPOT cone shaping (#2318). Mirrors `LightType::SPOT` in
// component_light_source.hpp. The cone factor smoothly falls off across a
// band from the nominal half-aperture to `kConeEdgeSoftness ×` that angle so
// the cone edge is anti-aliased rather than a hard step.
const int kLightTypeSpot = 3;
const float kConeEdgeSoftness = 1.15;

// Analytic SPOT falloff at world position `pos3D` for the light whose 0-based
// index is `lightIdx`. 1.0 inside the cone, smoothstep to 0.0 across the soft
// edge band, 0.0 outside. The apex is the light's TRUE (unclamped) origin so
// an out-of-window spot's cone stays oriented from its real position.
float spotConeFactor(int lightIdx, vec3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const vec3 axis = normalize(L.directionAndRadius.xyz);
    const vec3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4) {
        return 1.0;   // at the apex — fully lit, avoid a 0/0 direction.
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = radians(L.coneAndSeedAlpha.x * 0.5);
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, radians(90.0)));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
// Maps [0, ∞) → [0, 1) with a gentle shoulder that preserves color
// saturation in bright highlights better than Reinhard.
vec3 ACESFilm(vec3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main() {
    if (lightingEnabled == 0) {
        return;
    }

    const ivec2 size = imageSize(trixelColors);
    ivec2 pixel;
    if (perAxisRoute != 0) {
        // #2256: indirect dispatch over the compacted occupied-cell list, folded
        // into a capped 2-D workgroup grid by c_per_axis_cell_finalize —
        // idx = flat group index * tile + local flat index, guarded by the axis's
        // visibleCount, then decode the pixel from its linear cell.
        const uint groupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + gl_LocalInvocationIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = ivec2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = ivec2(gl_GlobalInvocationID.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    // Empty/background pixels: single-canvas uses 65535; per-axis uses INT_MAX (#1458).
    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    // A detached re-voxelize canvas (#1558) is lit by AO + directional sun + sky
    // only by DEFAULT (its slots 4/5 are inert placeholders). The opt-in
    // world-placed path (#1576 P4b-2, detachedWorldReceive.w != 0) instead has it
    // RECEIVE world sun-shadow + 128³ light-volume bleed at its recovered world
    // pos, like an attached GRID solid. The default path stays byte-identical.
    const bool detachedCanvas = isDetachedCanvas != 0.0;
    const bool worldReceive = detachedCanvas && detachedWorldReceive.w != 0.0;

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (#1458 per-axis / single-canvas, flip carrier #2207).
    const int rawDepth = decodeDepthRoute(encoded, perAxisRoute);
    // Decode the visible-triplet slot (#1278) → world FaceId → world-frame
    // six-face outward normal. Used by Lambert, the HDR sky-term, and the
    // world-receive sun-shadow normal — so hoist it above the shadow read.
    // The riser-polarity flip (#2207) selects the OPPOSITE same-axis face, so
    // a flipped silhouette riser shades with its true outward normal instead
    // of the inverted triplet one (the venetian near-black rows at 180°).
    const int slot = decodeSlot(encoded);
    const int faceId = visibleFaceIds[slot] ^ decodeFlipRoute(encoded, perAxisRoute);
    vec3 worldNormal = faceOutwardNormal6(faceId);

    // Recover this voxel's WORLD position once for an opt-in world-placed
    // detached solid (model pos + the entity's world cell origin); shared by the
    // sun-shadow receive below and the light-volume sample. The detached
    // re-voxelize canvas rasters cardinal (rasterYaw == 0, perAxisRoute == 0), so
    // trixelCanvasPixelToWorld3D recovers its pool-centered MODEL pos.
    vec3 worldReceivePos = vec3(0.0);
    if (worldReceive) {
        worldReceivePos = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        ) + detachedWorldReceive.xyz;
    }

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    float ao           = imageLoad(canvasAO, pixel).r;
    // Shadow factor: the world canvas reads its per-pixel COMPUTE_SUN_SHADOW
    // result; an opt-in world-placed detached solid re-runs that same cascade
    // lookup at its recovered world pos (receive — world iso depth = model
    // rawDepth + the offset's iso depth picks the cascade); a default detached
    // overlay stays forced fully lit (no shadow map).
    float shadow;
    if (worldReceive) {
        shadow = shadowsEnabled != 0
            ? worldSunShadowFactor(
                  worldReceivePos, worldNormal,
                  float(rawDepth) + detachedWorldReceive.x +
                      detachedWorldReceive.y + detachedWorldReceive.z
              )
            : 1.0;
    } else {
        shadow = detachedCanvas ? 1.0 : imageLoad(canvasSunShadow, pixel).r;
    }
    const vec4  src    = imageLoad(trixelColors, pixel);

    // Debug overlay short-circuits artistic shading and paints a false-
    // color representation of the selected lighting buffer.
    if (debugOverlayMode != 0) {
        vec3 debugColor = vec3(0.0);
        if (debugOverlayMode == 1) {
            debugColor = vec3(1.0 - ao, ao, 0.0);
        } else if (debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = vec3(level, level, 1.0);
        } else {
            debugColor = shadow >= 0.999 ? vec3(0.0) : vec3(1.0, 0.0, 1.0);
        }
        imageStore(trixelColors, pixel, vec4(debugColor, src.a));
        return;
    }

    // Fog cross-section CUT face (#2124 lit-cross-section follow-up): the interior
    // wall exposed at the vision boundary is geometrically buried, so the sun-shadow
    // map (baked from the full solid) reports it self-shadowed and the AO pass reads
    // it as a deep interior crease — together they render the cut wall as a dark
    // smear instead of a clean cross-section. Force it fully lit (shadow + AO = 1)
    // so it shades as a normal exposed face: Lambert + ambient + light-volume only.
    // The flag rides bit 29 of the stored id (stage 2); the `perAxisRoute == 0`
    // guard skips the read on the rotation route (id image unbound there), and
    // non-fog scenes never set the flag ⇒ byte-identical.
    if (perAxisRoute == 0 && decodeCutFace(imageLoad(trixelEntityIds, pixel).xy)) {
        ao = 1.0;
        shadow = 1.0;
    }

    // Sun direction lives in the world frame; the six-face `faceOutwardNormal6`
    // (decoded above into worldNormal) gives the matching world-frame normal.
    const float lambert = max(0.0, dot(worldNormal, sunDirection.xyz));
    // The sun-shadow darkens only the DIRECTIONAL (Lambert) term — ambient is
    // indirect fill light and is never blocked by the sun-shadow map. Folding
    // `shadow` into the whole `faceFactor` (the old `* shadow` below) collapsed
    // a fully self-shadowed face to pure black: a re-voxelize cube's -X/-Y side
    // facing away from the sun lands in its own cast shadow, lost its 0.4
    // ambient floor, and read as a missing/black face at every off-cardinal
    // pose. Unshadowed pixels (shadow == 1) are byte-identical to before.
    const float faceFactor =
        (sunAmbient + (1.0 - sunAmbient) * lambert * shadow) * sunIntensity;

    vec3 baseRgb;
    if (lutEnabled == 0) {
        baseRgb = src.rgb * ao * faceFactor;
    } else {
        // LUT palette shading: AO drives the X axis (light level) and pixel
        // luminance selects the palette row so highlights and shadows get
        // distinct cel-shade colour casts. The directional shadow is already
        // folded into faceFactor (ambient-preserving), so the LUT path composes
        // palette shading and shadows without needing a 3D LUT.
        const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
        const vec4  lut       = texture(paletteLUT, vec2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * faceFactor;
    }

    // Light-volume bleed: the world canvas (and per-axis camera canvases) sample
    // the shared 128³ volume; an opt-in world-placed detached solid samples it
    // too, at its recovered world pos (#1576 P4b-2). A default detached overlay
    // stays excluded (placeholder volume never sampled) — byte-identical.
    if (lightVolumeEnabled != 0 && (!detachedCanvas || worldReceive)) {
        // Recover the world voxel position of this pixel from the encoded
        // depth + iso offset, mirroring the math in c_compute_voxel_ao.glsl.
        // Subdivision-aware canvasOffset matches c_compute_voxel_ao.glsl.
        // At rasterYaw==0 the path collapses to master so yaw=0 stays
        // byte-identical; non-zero rasterYaw composes R(-rasterYaw)
        // afterward to recover world coordinates.
        // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
        // face-locally, so recover world-pos via isoPixelToPos3D; the single
        // canvas uses the cardinal-snap reconstruction. The shared world light
        // volume is then sampled the same way for both (per-axis canvases are only
        // allocated while rotating, so the cardinal fast path is byte-identical).
        // The world-placed detached solid reuses worldReceivePos (model + offset).
        // Smooth-yaw single-canvas recovery (#1719): while rotating, the main
        // canvas's remaining SDF/text content stores at the FULL visualYaw with
        // view-frame depth, so the light-volume sample position must use the
        // smooth inverse or the glow drifts off the surface as |residual| grows.
        // residualYaw == 0 (and every detached canvas, whose frame carries zero
        // yaw) keeps the byte-identical cardinal recovery.
        // Sub-cell recovery on the per-axis route — the volume sample must
        // land on the drawn surface (see perAxisCellToWorld3DSubCell).
        vec3 pos3D = worldReceive
            ? worldReceivePos
            : (perAxisRoute != 0
                ? perAxisCellToWorld3DSubCell(pixel, encoded, faceId, size, frameCanvasOffset, voxelRenderOptions)
                : (residualYaw != 0.0
                    ? trixelCanvasPixelToWorld3DSmoothYaw(
                          pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset,
                          voxelRenderOptions, visualYaw
                      )
                    : trixelCanvasPixelToWorld3D(
                          pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset,
                          voxelRenderOptions, rasterYaw
                      )));

        // Sample the light volume at the surface voxel. CLAMP_TO_EDGE
        // means out-of-volume samples read zero light (the border texels
        // were cleared during volume staging). The propagate pass stores
        // unattenuated emit color in rgb and residual strength in alpha,
        // so the visible contribution is `rgb * alpha` (linear falloff
        // with Manhattan distance, zero past the light's radius).
        // Phase 1c (#360): subtract the camera-anchored world origin so
        // the sample maps to the texel the seed/propagate passes wrote.
        const vec3 localPos =
            pos3D - vec3(lightVolumeWorldOrigin.xyz);
        const vec3 sampleCoord =
            (localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)) /
            vec3(kLightVolumeSize);
        const vec4 lightSample = texture(lightVolume, sampleCoord);
        vec3 light = lightSample.rgb * lightSample.a;

        // SPOT cone shaping (#2318). Gated on the has-SPOT flag
        // (lightVolumeWorldOrigin.w) so no-spot scenes skip the ID fetch +
        // light-list read entirely and stay byte-identical. Fetch the winning
        // light's ID (NEAREST — the surface voxel's own cell, not interpolated)
        // and, if it is a SPOT, attenuate its volume contribution by the
        // analytic cone factor. POINT/EMISSIVE winners keep the omni field.
        if (lightVolumeWorldOrigin.w != 0) {
            const ivec3 idCell = ivec3(floor(localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)));
            if (all(greaterThanEqual(idCell, ivec3(0))) &&
                all(lessThan(idCell, ivec3(int(kLightVolumeSize))))) {
                const int winId = int(round(imageLoad(lightVolumeId, idCell).r * 255.0));
                if (winId > 0 && int(lights[winId - 1].originAndType.w) == kLightTypeSpot) {
                    light *= spotConeFactor(winId - 1, pos3D);
                }
            }
        }
        baseRgb = baseRgb + src.rgb * light;
    }

    if (hdrEnabled != 0) {
        // Sky-term: upward-facing surfaces receive an additive
        // emissive contribution from the sky hemisphere, gated by
        // AO so recessed surfaces stay dark.
        if (skyIntensity > 0.0) {
            float skyFactor = max(0.0, worldNormal.z);
            baseRgb += skyColor.rgb * skyIntensity * skyFactor * ao;
        }

        // Exposure + ACES Filmic tonemap. The HDR dynamic range lives
        // in the float baseRgb; the tonemap compresses it to [0, 1]
        // before the RGBA8 imageStore.
        baseRgb = ACESFilm(baseRgb * exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0, 1.0);
    }

    imageStore(trixelColors, pixel, vec4(baseRgb, src.a));
}
