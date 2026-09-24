#include "ir_iso_common.metal"
#if IR_SHAPE_RECEIVER
#include "ir_receiver_face.metal"
#endif
#include "ir_per_axis_lighting.metal"
// FrameDataSun + the sun-depth buffer cascade lookup (worldSunShadowFactor) —
// for the opt-in detached re-voxelize world-receive path. Shared with
// c_compute_sun_shadow.
#include "ir_sun_shadow_sample.metal"
// GPULightSource layout, light-volume extents, spotConeFactor, ACESFilm, plus
// the FrameDataLightingToTrixel + LightVolumeParams UBO layouts this kernel and
// c_light_overflow_faces both bind.
#include "ir_world_lighting.metal"

// Mirrors shaders/c_lighting_to_trixel.glsl.

// Per-axis empty-cell compaction: on the per-axis route
// (perAxisRoute != 0) this kernel is dispatched indirectly over only each axis's
// OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass). compactedCells
// holds the occupied linear cell indices; cellDrawArgs carries visibleCount at
// [kDispatchArgsBaseUint + 3] for the 1-D bound guard. GLSL twin's bindings 25/26.
constant uint kDispatchArgsBaseUint = 8u;
constant uint kPerAxisCellComputeTile = 256u;

void writeLitTrixel(texture2d<float, access::read_write> canvas,
    device SourceVoxelFaces& faces, bool sourceMode, uint index, uint2 pixel, float4 color) {
    if (sourceMode) faces.faces[index].color = color;
    else canvas.write(color, pixel);
}

kernel void IR_LIGHTING_KERNEL_NAME(
    device SourceVoxelFaces& sourceFaces [[buffer(8)]],
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    constant FrameDataVoxelToTrixel& voxelFrameData [[buffer(7)]],
    constant FrameDataSun& sunFrameData [[buffer(29)]],
    constant LightVolumeParams& lightVolumeParams [[buffer(23)]],
    // Baked sun-aligned depth map — read by the detached world-receive path
    // to re-run the cascade lookup at a world-placed voxel's pos.
    device const uint* sunDepthBuf [[buffer(28)]],
    // Light list for SPOT cone shaping: the winning-light ID indexes
    // this to recover cone axis/aperture/apex. Bound transiently at slot 4 by
    // LIGHTING_TO_TRIXEL; only read on the spot path.
    device const GPULightSource* lights [[buffer(4)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasAO [[texture(2)]],
    texture2d<float, access::sample> paletteLUT [[texture(3)]],
    // Unit 4 — Metal flattens texture/image tables into a shared slot
    // space; cannot collide with paletteLUT(3) or lightVolume(5).
    texture2d<float, access::read> canvasSunShadow [[texture(4)]],
    texture3d<float, access::sample> lightVolume [[texture(5)]],
    // Entity-id channel: read ONLY for the fog cut-face flag (bit 29, set by
    // stage 2, read via decodeCutFace). Bound at unit 6 on the single-canvas +
    // detached routes; the `perAxisRoute == 0` guard skips the read on the
    // rotation route. GLSL twin's binding 6.
    texture2d<uint, access::read> trixelEntityIds [[texture(6)]],
    // Winning-light ID volume, unit 7. `.r` = light index+1 (÷255) of
    // the flood winner per cell. Read only on the spot path; bound every tick
    // so Metal's slot table is populated. GLSL twin's binding 7.
    texture3d<float, access::read> lightVolumeId [[texture(7)]],
    // Per-axis empty-cell compaction — GLSL twin's bindings 25/26.
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    if (frameData.lightingEnabled == 0) {
        return;
    }

    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    int2 pixel;
    float sourceAO = 1.0;
    float3 sourceViewCenter = float3(0.0);
    const bool sourceMode = voxelFrameData.isDetachedCanvas > 1.5f;
    const uint sourceIndex = globalId.x + globalId.y * uint(size.x);
    if (sourceMode) {
        if (globalId.x >= uint(size.x) || sourceIndex >= atomic_load_explicit(&sourceFaces.count, memory_order_relaxed)) return;
        const SourceVoxelFace source = sourceFaces.faces[sourceIndex];
        const int density = effectiveTrixelSubdivisionScale(voxelFrameData.voxelRenderOptions);
        const float2x2 deformX = float2x2(voxelFrameData.faceDeform[0].xy, voxelFrameData.faceDeform[0].zw);
        const float2x2 deformY = float2x2(voxelFrameData.faceDeform[1].xy, voxelFrameData.faceDeform[1].zw);
        const int2 frameOffset = trixelFrameOffset(
            voxelFrameData.trixelCanvasOffsetZ1, voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions);
        const DetachedFaceFootprint face = detachedFaceFootprint(
            source.centerAndFace.xyz, int(source.centerAndFace.w), density, 0,
            deformX, deformY, voxelFrameData.voxelDepthAxis.xyz, frameOffset);
        const float2 center = face.origin + 0.5 * float(density) * (face.edgeU + face.edgeV);
        pixel = clamp(int2(floor(center)), int2(0), size - 1);
        const float2 rasterOrigin = float2(frameOffset) + float2(float(density));
        const float centerDepth = face.depth.x + dot(
            face.uvOrigin + float2(0.5 * float(density)), face.depth.yz);
        sourceViewCenter = isoPositionToPos3D(center - rasterOrigin, centerDepth) / float(density);
        if (sunFrameData.aoEnabled != 0) {
            // The source face owns its receiver; the depth canvas only supplies occluders.
            const float3 normal = detachedFaceViewNormal(
                int(source.centerAndFace.w), deformX, deformY, voxelFrameData.voxelDepthAxis.xyz);
            const int parity = localTrixelOriginParity(voxelFrameData.trixelCanvasOffsetZ1);
            for (int direction = 0; direction < 4; ++direction) {
                const float2 tangent = direction < 2 ? face.edgeU : face.edgeV;
                const float sign = (direction & 1) == 0 ? -1.0 : 1.0;
                const int2 neighborPixel = int2(floor(center + sign * float(density) * tangent));
                if (any(neighborPixel < int2(0)) || any(neighborPixel >= size)) continue;
                const int neighbor = trixelDistances.read(uint2(neighborPixel)).x;
                if (neighbor >= 65535 || decodeSlot(neighbor) == (int(source.centerAndFace.w) >> 1)) continue;
                const float2 neighborIso = localTrixelCellCentroid(neighborPixel, parity) - rasterOrigin;
                const float3 occluder = isoPositionToPos3D(neighborIso, float(decodeDepthSingle(neighbor))) / float(density);
                const float height = dot(occluder - sourceViewCenter, normal);
                if (height > 0.125 && height < 1.5) sourceAO -= 0.10;
            }
        }
    } else if (voxelFrameData.perAxisRoute != 0) {
        // The compacted-cell dispatch is folded into a capped 2-D threadgroup
        // grid by c_per_axis_cell_finalize (groupsX capped, remainder in groupsY).
        const uint groupIndex = groupId.x + groupId.y * numGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + localIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = int2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = int2(globalId.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    const int encoded = sourceMode ? encodeDepthWithFace(0, int(sourceFaces.faces[sourceIndex].centerAndFace.w) >> 1) : trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel; single-canvas keeps 65535.
    if (encoded >= (voxelFrameData.perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    const float4 sourceColor = sourceMode ? sourceFaces.faces[sourceIndex].color : trixelColors.read(uint2(pixel));
    // A detached re-voxelize canvas is lit by AO + directional sun + sky
    // only by DEFAULT; the opt-in world-placed path
    // (detachedWorldReceive.w != 0) instead has it RECEIVE world sun-shadow + 128³
    // light-volume bleed at its recovered world pos, like a GRID solid.
    // Mirrors c_lighting_to_trixel.glsl.
    const bool detachedCanvas = voxelFrameData.isDetachedCanvas != 0.0f;
    const bool worldReceive = detachedCanvas && voxelFrameData.detachedWorldReceive.w != 0.0f;

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, including the flip carrier).
    const int rawDepth = decodeDepthRoute(encoded, voxelFrameData.perAxisRoute);
    // The riser-polarity flip selects the OPPOSITE same-axis face, so a flipped
    // silhouette riser shades with its true outward normal instead of the
    // inverted triplet one.
    const int slot = decodeSlot(encoded);
    const int faceId =
        sourceMode ? int(sourceFaces.faces[sourceIndex].centerAndFace.w) : voxelFrameData.visibleFaceIds[slot] ^ decodeFlipRoute(encoded, voxelFrameData.perAxisRoute);
    float3 worldNormal = faceOutwardNormal6(faceId);
    if (frameData.normalOptions.x != 0 && !detachedCanvas && voxelFrameData.perAxisRoute == 0 && voxelFrameData.residualYaw != 0.0f) {
        worldNormal = rotateYawZInv(faceOutwardNormal(slot), voxelFrameData.visualYaw);
        if (decodeFlipRoute(encoded, voxelFrameData.perAxisRoute) != 0) worldNormal = -worldNormal;
    }
    if (detachedCanvas && voxelFrameData.visibleFaceIds.w == 0) {
        worldNormal = rotateByQuat(detachedFaceViewNormal(
            faceId,
            float2x2(voxelFrameData.faceDeform[0].xy, voxelFrameData.faceDeform[0].zw),
            float2x2(voxelFrameData.faceDeform[1].xy, voxelFrameData.faceDeform[1].zw),
            voxelFrameData.voxelDepthAxis.xyz
        ), frameData.detachedViewToWorld);
    }

    // The private raster is camera-relative; lighting and cascade selection use world units.
    float3 worldReceivePos = float3(0.0f);
    if (worldReceive && sourceMode) {
        worldReceivePos = rotateByQuat(sourceViewCenter, frameData.detachedViewToWorld)
                        + voxelFrameData.detachedWorldReceive.xyz;
    } else if (worldReceive) {
        worldReceivePos = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, voxelFrameData.trixelCanvasOffsetZ1,
            voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions,
            voxelFrameData.rasterYaw
        );
        // Raster coordinates use cell corners; geometry uses voxel centers.
        if (voxelFrameData.visibleFaceIds.w == 2) {
            const int2 isoRel = trixelCanvasPixelToIsoRel(
                pixel, voxelFrameData.trixelCanvasOffsetZ1, voxelFrameData.frameCanvasOffset,
                voxelFrameData.voxelRenderOptions);
            // A displayed triangle centroid has iso delta (f - 1, -1) and
            // depth delta +1 from the stored face-origin sample. Its in-plane
            // centroid fraction f alternates with local triangle orientation.
            const float centroidFraction = ((isoRel.x + isoRel.y) & 1) != 0 ? 1.0f / 3.0f : 2.0f / 3.0f;
            worldReceivePos += float3(1.0f - 0.5f * centroidFraction, 0.5f * centroidFraction, 0.0f)
                / float(effectiveTrixelSubdivisionScale(voxelFrameData.voxelRenderOptions));
        }
        worldReceivePos -= kVoxelRasterCellAnchor;
        // Detached pool cells already include inverse camera rotation.
        worldReceivePos = rotateByQuat(worldReceivePos, frameData.detachedViewToWorld)
                        + voxelFrameData.detachedWorldReceive.xyz;
        worldNormal = rotateByQuat(worldNormal, frameData.detachedViewToWorld);
    }

    // World receiver relative to its canvas raster origin, encoded over [-2, 2].
    if (frameData.debugOverlayMode == 9) {
        const float4 src = sourceColor;
        const float3 positionColor = worldReceive
            ? (worldReceivePos - voxelFrameData.detachedWorldReceive.xyz) * 0.25f + 0.5f
            : float3(0.0f);
        writeLitTrixel(trixelColors, sourceFaces, sourceMode, sourceIndex, uint2(pixel), float4(positionColor, src.a));
        return;
    }

#if IR_SHAPE_RECEIVER
    worldNormal = receiverFaceNormal(canvasSunShadow.read(uint2(pixel)).a, worldNormal);
#endif
    if (frameData.debugOverlayMode == 8) {
        const float alpha = sourceColor.a;
        writeLitTrixel(trixelColors, sourceFaces, sourceMode, sourceIndex, uint2(pixel), float4(worldNormal * 0.5f + 0.5f, alpha));
        return;
    }

    float ao = sourceMode ? sourceAO : canvasAO.read(uint2(pixel)).r;
    bool continuousShadow = sourceMode && worldReceive && sunFrameData.shadowsEnabled != 0;
    float shadow;
    if (worldReceive) {
        shadow = 1.0;
        if (sunFrameData.shadowsEnabled != 0 && !continuousShadow) {
            shadow = (sourceMode || voxelFrameData.visibleFaceIds.w == 2)
                ? worldSurfaceSunShadowFactor(worldReceivePos, worldNormal, pos3DtoDistance(worldReceivePos), frameData.detachedViewToWorld, sunFrameData, sunDepthBuf)
                : worldSunShadowFactor(worldReceivePos, worldNormal, pos3DtoDistance(worldReceivePos), sunFrameData, sunDepthBuf);
        }
    } else {
        shadow = detachedCanvas ? 1.0f : canvasSunShadow.read(uint2(pixel)).r;
    }
    const float4 src    = sourceColor;

    if (frameData.debugOverlayMode != 0) {
        if (continuousShadow && frameData.debugOverlayMode != 1) {
            sourceFaces.faces[sourceIndex].worldCenterAndAO = float4(worldReceivePos, ao);
            sourceFaces.faces[sourceIndex].owner.z = frameData.debugOverlayMode == 2
                ? kSourceLightingAOShadow : kSourceLightingShadow;
            return;
        }
        float3 debugColor = float3(0.0f);
        if (frameData.debugOverlayMode == 1) {
            debugColor = float3(1.0f - ao, ao, 0.0f);
        } else if (frameData.debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = float3(level, level, 1.0f);
        } else {
            debugColor = shadow >= 0.999f ? float3(0.0f) : float3(1.0f, 0.0f, 1.0f);
        }
        writeLitTrixel(trixelColors, sourceFaces, sourceMode, sourceIndex, uint2(pixel), float4(debugColor, src.a));
        return;
    }

    // Fog cross-section CUT face. The interior wall exposed at the vision
    // boundary is geometrically buried, so the sun-shadow map reports it
    // self-shadowed and AO reads it as a deep crease; force it fully lit
    // (shadow + AO = 1) so it shades as a clean exposed face. Flag rides bit 29
    // of the stored id; the `perAxisRoute == 0` guard skips the read on the
    // rotation route (id image unbound there).
    if (voxelFrameData.perAxisRoute == 0 &&
        decodeCutFace(sourceMode ? sourceFaces.faces[sourceIndex].owner.xy : trixelEntityIds.read(uint2(pixel)).xy)) {
        ao = 1.0f;
        shadow = 1.0f;
        continuousShadow = false;
    }

    // Sun direction and worldNormal are both world-frame.
    const float lambert = max(0.0f, dot(worldNormal, sunFrameData.sunDirection.xyz));
    // Sun-shadow darkens only the directional term; ambient is indirect fill and
    // is never blocked, so a fully self-shadowed face keeps its ambient floor
    // instead of collapsing to pure black.
    const float faceFactor =
        (sunFrameData.sunAmbient + (1.0f - sunFrameData.sunAmbient) * lambert * shadow) *
        sunFrameData.sunIntensity;

    float3 baseRgb;
    float3 materialRgb;
    if (frameData.lutEnabled == 0) {
        materialRgb = src.rgb * ao;
        baseRgb = materialRgb * faceFactor;
    } else {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        const float  luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
        const float4 lut       = paletteLUT.sample(s, float2(ao, luminance));
        materialRgb = src.rgb * lut.rgb;
        baseRgb = materialRgb * faceFactor;
    }

    if (continuousShadow) {
        sourceFaces.faces[sourceIndex].worldCenterAndAO = float4(worldReceivePos, ao);
        sourceFaces.faces[sourceIndex].directSunAndExposure = float4(
            materialRgb * (1.0 - sunFrameData.sunAmbient) * lambert * sunFrameData.sunIntensity, frameData.exposure);
        sourceFaces.faces[sourceIndex].owner.z = frameData.hdrEnabled != 0 ? kSourceLightingHDR : kSourceLightingLinear;
        baseRgb = materialRgb * sunFrameData.sunAmbient * sunFrameData.sunIntensity;
    }

    // Light-volume bleed: the world / per-axis camera canvases sample the shared
    // 128³ volume; an opt-in world-placed detached solid samples it too, at its
    // recovered world pos. A default detached overlay stays excluded. Mirrors
    // GLSL.
    if (frameData.lightVolumeEnabled != 0 && (!detachedCanvas || worldReceive)) {
        // World-position recovery for the volume sample — mirrors GLSL:
        //   * world-placed detached solid: worldReceivePos (model + offset);
        //   * per-axis canvas: stores the world frame face-locally; sub-cell
        //     recovery, not lattice-only, so the volume sample lands on the
        //     drawn surface;
        //   * single canvas with residualYaw != 0: the remaining SDF/text
        //     content stores at the FULL visualYaw with view-frame depth, so
        //     the smooth inverse is required or the glow drifts off the surface
        //     as |residual| grows;
        //   * otherwise (including every detached canvas, whose frame carries
        //     zero yaw) the cardinal-snap reconstruction.
        float3 pos3D = worldReceive
            ? worldReceivePos
            : (voxelFrameData.perAxisRoute != 0
                ? perAxisCellToWorld3DSubCell(
                      pixel, encoded, faceId, size,
                      voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions
                  )
                : (voxelFrameData.residualYaw != 0.0
                    ? trixelCanvasPixelToWorld3DSmoothYaw(
                          pixel,
                          rawDepth,
                          voxelFrameData.trixelCanvasOffsetZ1,
                          voxelFrameData.frameCanvasOffset,
                          voxelFrameData.voxelRenderOptions,
                          voxelFrameData.visualYaw
                      )
                    : trixelCanvasPixelToWorld3D(
                          pixel,
                          rawDepth,
                          voxelFrameData.trixelCanvasOffsetZ1,
                          voxelFrameData.frameCanvasOffset,
                          voxelFrameData.voxelRenderOptions,
                          voxelFrameData.rasterYaw
                      )));

        constexpr sampler volumeSampler(
            filter::nearest, address::clamp_to_edge
        );
        const float3 localPos =
            pos3D - float3(lightVolumeParams.worldOriginVoxel.xyz);
        const float3 sampleCoord =
            (localPos + float3(kLightVolumeHalfExtent) + float3(0.5)) /
            float3(kLightVolumeSize);
        const float4 lightSample = lightVolume.sample(volumeSampler, sampleCoord);
        float3 light = lightSample.rgb * lightSample.a;

        // The winning light's ID is fetched at the surface voxel's own cell
        // (NEAREST — not interpolated).
        if (lightVolumeParams.worldOriginVoxel.w != 0) {
            const int3 idCell = int3(floor(localPos + float3(kLightVolumeHalfExtent) + float3(0.5)));
            if (all(idCell >= int3(0)) && all(idCell < int3(int(kLightVolumeSize)))) {
                const int winId = roundHalfUp(lightVolumeId.read(uint3(idCell)).r * 255.0f);
                if (winId > 0 && int(lights[winId - 1].originAndType.w) == kLightTypeSpot) {
                    light *= spotConeFactor(lights, winId - 1, pos3D);
                }
            }
        }
        baseRgb = baseRgb + src.rgb * light;
    }

    if (frameData.hdrEnabled != 0) {
        if (frameData.skyIntensity > 0.0f) {
            float skyFactor = max(0.0f, worldNormal.z);
            baseRgb += frameData.skyColor.rgb * frameData.skyIntensity * skyFactor * ao;
        }
        if (!continuousShadow) baseRgb = ACESFilm(baseRgb * frameData.exposure);
    } else if (!continuousShadow) {
        baseRgb = clamp(baseRgb, 0.0f, 1.0f);
    }

    writeLitTrixel(trixelColors, sourceFaces, sourceMode, sourceIndex, uint2(pixel), float4(baseRgb, src.a));
}
