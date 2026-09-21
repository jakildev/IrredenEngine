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
// dynamic range lives entirely in shader-local variables; the canvas
// stays RGBA8.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// Shared caster/receiver sun-space projection.
#include "ir_sun_projection.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), and worldSunShadowFactor() — for
// the opt-in detached re-voxelize world-receive path. Shared with
// c_compute_sun_shadow.
#include "ir_sun_shadow_sample.glsl"
// GPULightSource list (slot 4), light-volume extents, spotConeFactor, ACESFilm.
#include "ir_world_lighting.glsl"

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
    uniform vec4  detachedViewToWorld;
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = lighting
    // a per-axis canvas, reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // 1.0 for a detached entity canvas (re-voxelize solid), 0.0 for the world
    // canvas. A detached re-voxelize canvas carries no sun-shadow map / light
    // volume — unless it opts into world receive, the pass forces shadow = 1.0
    // and disables the light-volume term so slots 4/5 (inert placeholders) are
    // never sampled.
    uniform float isDetachedCanvas;
    // Per-face 2x2 residual-yaw deformation, cols packed .xy / .zw. The
    // source-face path below reconstructs each detached face's footprint
    // from it, so these slots are read here, not just reserved for layout.
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5); must match c_voxel_to_trixel_stage_1.glsl.
    // Lighting maps the decoded depth slot → world FaceId for the
    // six-face outward normal used by Lambert + the HDR sky-term.
    uniform ivec4 visibleFaceIds;
    // Iso depth axis; (1,1,1) on the world canvas, per-axis on a detached
    // re-voxelize canvas. Read by the source-face footprint and normal.
    uniform vec4 voxelDepthAxis;
    // World-receive offset. `.xyz` = the opt-in world-placed
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
// Entity-id channel: read ONLY to recover the fog cut-face flag (bit 29, set by
// c_voxel_to_trixel_stage_2, read via decodeCutFace). Bound at image unit 6 on
// the single-canvas + detached routes; the per-axis rotation route leaves it
// unbound and the `perAxisRoute == 0` guard skips the read. Non-fog scenes never
// set the flag.
layout(rg32ui, binding = 6) readonly uniform uimage2D trixelEntityIds;
// Winning-light ID volume, image unit 7. `.r` = the index+1 (÷255) of the light
// that won each cell's flood contest. Fetched (NEAREST — no interpolation) only
// when the frame gathered a SPOT light (`lightVolumeWorldOrigin.w != 0`). Bound
// every tick so Metal's slot table is populated.
layout(rgba8, binding = 7) readonly uniform image3D lightVolumeId;

// Per-axis empty-cell compaction: on the per-axis route (perAxisRoute !=
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

// The light volume is camera-anchored. The CPU uploads
// `lightVolumeWorldOrigin` (the world voxel that maps to the volume's
// center texel) each frame; subtract it from `pos3D` before converting to
// a sample coordinate. Mirrors LightVolumeParams in ir_render_types.hpp —
// `.xyz` is the volume origin, `.w` is the has-SPOT flag.
// The layout must match the propagate/seed UBO layout
// (c_seed_light_volume.glsl, c_propagate_light_volume.glsl). Lighting
// only reads the origin; leading-underscore names mark the unused slots.
layout(std140, binding = 23) uniform LightVolumeParams {
    int _gridSize;
    int _halfExtent;
    int _lightCount;
    float _stepFalloff;
    ivec4 lightVolumeWorldOrigin;
};

layout(std430, binding = 8) buffer SourceVoxelFaces {
    uint sourceIndexCount; uint sourceFaceCount; uint sourcePadding[6];
    SourceVoxelFace sourceFaces[];
};
void writeLitTrixel(bool sourceMode, uint index, ivec2 pixel, vec4 color) {
    if (sourceMode) sourceFaces[index].color = color;
    else imageStore(trixelColors, pixel, color);
}

void main() {
    if (lightingEnabled == 0) {
        return;
    }

    const ivec2 size = imageSize(trixelColors);
    ivec2 pixel;
    float sourceAO = 1.0;
    const bool sourceMode = isDetachedCanvas > 1.5;
    const uint sourceIndex = gl_GlobalInvocationID.x + gl_GlobalInvocationID.y * uint(size.x);
    if (sourceMode) {
        if (gl_GlobalInvocationID.x >= uint(size.x) || sourceIndex >= sourceFaceCount) return;
        const SourceVoxelFace source = sourceFaces[sourceIndex];
        const int density = effectiveTrixelSubdivisionScale(voxelRenderOptions);
        const mat2 deformX = mat2(faceDeform[0].xy, faceDeform[0].zw);
        const mat2 deformY = mat2(faceDeform[1].xy, faceDeform[1].zw);
        const ivec2 frameOffset = trixelFrameOffset(
            trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
        const DetachedFaceFootprint face = detachedFaceFootprint(
            source.centerAndFace.xyz, int(source.centerAndFace.w), density, 0,
            deformX, deformY, voxelDepthAxis.xyz, frameOffset);
        const vec2 center = face.origin + 0.5 * float(density) * (face.edgeU + face.edgeV);
        pixel = clamp(ivec2(floor(center)), ivec2(0), size - 1);
        if (aoEnabled != 0) {
            // The source face owns its receiver; the depth canvas only supplies occluders.
            const vec2 rasterOrigin = vec2(frameOffset) + vec2(float(density));
            const float centerDepth = face.depth.x + dot(
                face.uvOrigin + vec2(0.5 * float(density)), face.depth.yz);
            const vec3 receiver = isoPositionToPos3D(center - rasterOrigin, centerDepth) / float(density);
            const vec3 normal = detachedFaceViewNormal(
                int(source.centerAndFace.w), deformX, deformY, voxelDepthAxis.xyz);
            const int parity = localTrixelOriginParity(trixelCanvasOffsetZ1);
            for (int direction = 0; direction < 4; ++direction) {
                const vec2 tangent = direction < 2 ? face.edgeU : face.edgeV;
                const float sign = (direction & 1) == 0 ? -1.0 : 1.0;
                const ivec2 neighborPixel = ivec2(floor(center + sign * float(density) * tangent));
                if (any(lessThan(neighborPixel, ivec2(0))) || any(greaterThanEqual(neighborPixel, size))) continue;
                const int neighbor = imageLoad(trixelDistances, neighborPixel).x;
                if (neighbor >= 65535 || decodeSlot(neighbor) == (int(source.centerAndFace.w) >> 1)) continue;
                const vec2 neighborIso = localTrixelCellCentroid(neighborPixel, parity) - rasterOrigin;
                const vec3 occluder = isoPositionToPos3D(neighborIso, float(decodeDepthSingle(neighbor))) / float(density);
                const float height = dot(occluder - receiver, normal);
                if (height > 0.125 && height < 1.5) sourceAO -= 0.10;
            }
        }
    } else if (perAxisRoute != 0) {
        // Indirect dispatch over the compacted occupied-cell list, folded
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

    // Empty/background pixels: single-canvas uses 65535; per-axis uses INT_MAX.
    const int encoded = sourceMode ? encodeDepthWithFace(0, int(sourceFaces[sourceIndex].centerAndFace.w) >> 1) : imageLoad(trixelDistances, pixel).x;
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    const vec4 sourceColor = sourceMode ? sourceFaces[sourceIndex].color : imageLoad(trixelColors, pixel);
    // A detached re-voxelize canvas is lit by AO + directional sun + sky
    // only by DEFAULT (its slots 4/5 are inert placeholders). The opt-in
    // world-placed path (detachedWorldReceive.w != 0) instead has it
    // RECEIVE world sun-shadow + 128³ light-volume bleed at its recovered world
    // pos, like an attached GRID solid.
    const bool detachedCanvas = isDetachedCanvas != 0.0;
    const bool worldReceive = detachedCanvas && detachedWorldReceive.w != 0.0;

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, including the flip carrier).
    const int rawDepth = decodeDepthRoute(encoded, perAxisRoute);
    // Visible-triplet slot → world FaceId → world-frame six-face outward normal,
    // used by Lambert, the HDR sky-term, and the world-receive sun-shadow normal.
    // The riser-polarity flip selects the OPPOSITE same-axis face, so a flipped
    // silhouette riser shades with its true outward normal instead of the
    // inverted triplet one.
    const int slot = decodeSlot(encoded);
    const int faceId = sourceMode ? int(sourceFaces[sourceIndex].centerAndFace.w) : visibleFaceIds[slot] ^ decodeFlipRoute(encoded, perAxisRoute);
    vec3 worldNormal = faceOutwardNormal6(faceId);
    if (detachedCanvas && visibleFaceIds.w == 0) {
        worldNormal = rotateByQuat(detachedFaceViewNormal(
            faceId,
            mat2(faceDeform[0].xy, faceDeform[0].zw),
            mat2(faceDeform[1].xy, faceDeform[1].zw),
            voxelDepthAxis.xyz
        ), detachedViewToWorld);
    }

    // The private raster is camera-relative; lighting and cascade selection use world units.
    vec3 worldReceivePos = vec3(0.0);
    if (worldReceive) {
        worldReceivePos = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        if (visibleFaceIds.w == 2) {
            const ivec2 isoRel = trixelCanvasPixelToIsoRel(
                pixel, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
            // A displayed triangle centroid has iso delta (f - 1, -1) and
            // depth delta +1 from the stored face-origin sample. Its in-plane
            // centroid fraction f alternates with local triangle orientation.
            const float centroidFraction = ((isoRel.x + isoRel.y) & 1) != 0 ? 1.0 / 3.0 : 2.0 / 3.0;
            worldReceivePos += vec3(1.0 - 0.5 * centroidFraction, 0.5 * centroidFraction, 0.0)
                / float(effectiveTrixelSubdivisionScale(voxelRenderOptions));
        }
        // Raster coordinates use cell corners; geometry uses voxel centers.
        worldReceivePos -= kVoxelRasterCellAnchor;
        // Detached pool cells already include inverse camera rotation.
        worldReceivePos = rotateByQuat(worldReceivePos, detachedViewToWorld)
                        + detachedWorldReceive.xyz;
        worldNormal = rotateByQuat(worldNormal, detachedViewToWorld);
    }

    // World receiver relative to its canvas raster origin, encoded over [-2, 2].
    if (debugOverlayMode == 9) {
        const vec4 src = sourceColor;
        const vec3 positionColor = worldReceive
            ? (worldReceivePos - detachedWorldReceive.xyz) * 0.25 + 0.5
            : vec3(0.0);
        writeLitTrixel(sourceMode, sourceIndex, pixel, vec4(positionColor, src.a));
        return;
    }

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    if (debugOverlayMode == 8) {
        const float alpha = sourceColor.a;
        writeLitTrixel(sourceMode, sourceIndex, pixel, vec4(worldNormal * 0.5 + 0.5, alpha));
        return;
    }

    float ao = sourceMode ? sourceAO : imageLoad(canvasAO, pixel).r;
    float shadow;
    if (worldReceive) {
        shadow = 1.0;
        if (shadowsEnabled != 0) {
            shadow = visibleFaceIds.w == 2
                ? worldSurfaceSunShadowFactor(worldReceivePos, worldNormal, pos3DtoDistance(worldReceivePos))
                : worldSunShadowFactor(worldReceivePos, worldNormal, pos3DtoDistance(worldReceivePos));
        }
    } else {
        shadow = detachedCanvas ? 1.0 : imageLoad(canvasSunShadow, pixel).r;
    }
    const vec4  src    = sourceColor;

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
        writeLitTrixel(sourceMode, sourceIndex, pixel, vec4(debugColor, src.a));
        return;
    }

    // Fog cross-section CUT face: the interior wall exposed at the vision boundary
    // is geometrically buried, so the sun-shadow map (baked from the full solid)
    // reports it self-shadowed and the AO pass reads it as a deep interior crease —
    // together they would render the cut wall as a dark smear instead of a clean
    // cross-section. Force it fully lit (shadow + AO = 1) so it shades as a normal
    // exposed face: Lambert + ambient + light-volume only. The flag rides bit 29 of
    // the stored id (stage 2); the `perAxisRoute == 0` guard skips the read on the
    // rotation route (id image unbound there).
    if (perAxisRoute == 0 && decodeCutFace(sourceMode ? sourceFaces[sourceIndex].owner.xy : imageLoad(trixelEntityIds, pixel).xy)) {
        ao = 1.0;
        shadow = 1.0;
    }

    // Sun direction and worldNormal are both world-frame.
    const float lambert = max(0.0, dot(worldNormal, sunDirection.xyz));
    // The sun-shadow darkens only the DIRECTIONAL (Lambert) term — ambient is
    // indirect fill light and is never blocked by the sun-shadow map. Folding
    // `shadow` into the whole `faceFactor` collapses a fully self-shadowed face
    // (e.g. a re-voxelize cube's side facing away from the sun, inside its own
    // cast shadow) to pure black instead of its ambient floor.
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
    // too, at its recovered world pos. A default detached overlay stays excluded
    // (placeholder volume never sampled).
    if (lightVolumeEnabled != 0 && (!detachedCanvas || worldReceive)) {
        // World-position recovery for the volume sample:
        //   * world-placed detached solid: worldReceivePos (model + offset);
        //   * per-axis canvas: stores the world frame face-locally; sub-cell
        //     recovery, not lattice-only, so the volume sample lands on the
        //     drawn surface;
        //   * single canvas with residualYaw != 0: the remaining SDF/text
        //     content stores at the FULL visualYaw with view-frame depth, so
        //     the smooth inverse is required or the glow drifts off the surface
        //     as |residual| grows;
        //   * otherwise (including every detached canvas, whose frame carries
        //     zero yaw) the cardinal-snap reconstruction, with the same
        //     subdivision-aware canvasOffset as c_compute_voxel_ao.glsl and
        //     R(-rasterYaw) composed afterward.
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

        // CLAMP_TO_EDGE means out-of-volume samples read zero light (the
        // border texels are cleared during volume staging). The propagate
        // pass stores unattenuated emit color in rgb and residual strength
        // in alpha, so the visible contribution is `rgb * alpha` (linear
        // falloff with Manhattan distance, zero past the light's radius).
        // Subtracting the camera-anchored world origin maps the sample onto
        // the texel the seed/propagate passes wrote.
        const vec3 localPos =
            pos3D - vec3(lightVolumeWorldOrigin.xyz);
        const vec3 sampleCoord =
            (localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)) /
            vec3(kLightVolumeSize);
        const vec4 lightSample = texture(lightVolume, sampleCoord);
        vec3 light = lightSample.rgb * lightSample.a;

        // SPOT cone shaping, gated on the has-SPOT flag
        // (lightVolumeWorldOrigin.w). The winning light's ID is fetched
        // NEAREST — the surface voxel's own cell, not interpolated — and a
        // SPOT winner's volume contribution is attenuated by the analytic
        // cone factor. POINT/EMISSIVE winners keep the omni field.
        if (lightVolumeWorldOrigin.w != 0) {
            const ivec3 idCell = ivec3(floor(localPos + vec3(kLightVolumeHalfExtent) + vec3(0.5)));
            if (all(greaterThanEqual(idCell, ivec3(0))) &&
                all(lessThan(idCell, ivec3(int(kLightVolumeSize))))) {
                const int winId = roundHalfUp(imageLoad(lightVolumeId, idCell).r * 255.0);
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

    writeLitTrixel(sourceMode, sourceIndex, pixel, vec4(baseRgb, src.a));
}
