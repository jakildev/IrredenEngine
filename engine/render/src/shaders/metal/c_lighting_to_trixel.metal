#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
// FrameDataSun + the sun-depth buffer cascade lookup (worldSunShadowFactor) —
// for the opt-in detached re-voxelize world-receive path (#1576 P4b-2). Shared
// with c_compute_sun_shadow; replaces this kernel's former local FrameDataSun.
#include "ir_sun_shadow_sample.metal"

// Mirrors shaders/c_lighting_to_trixel.glsl. Screen-space lighting
// application pass — modulates trixelColors.rgb by (AO × sun-shadow),
// with an optional LUT palette shading path keyed off lutEnabled and
// an optional flood-fill light-volume additive contribution keyed off
// lightVolumeEnabled. When hdrEnabled is set, computes in unclamped
// float precision, adds the sky-term contribution, applies exposure,
// and tonemaps via the ACES Filmic curve before writing back to the
// canvas.

struct FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    int   lightVolumeEnabled;
    float debugLightLevel;
    int   debugOverlayMode;
    int   hdrEnabled;
    float exposure;
    float skyIntensity;
    float4 skyColor;
};

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
constant float kLightVolumeSize = 128.0;
constant float kLightVolumeHalfExtent = 64.0;

// Layout must match the propagate/seed UBO layout so the shared buffer
// binding works. Lighting reads `worldOriginVoxel.xyz` (volume origin) and
// `.w` (has-SPOT flag, #2318).
struct LightVolumeParams {
    int   _gridSize;
    int   _halfExtent;
    int   _lightCount;
    float _stepFalloff;
    int4  worldOriginVoxel;
};

// SPOT cone shaping (#2318). Mirrors GPULightSource / LightType::SPOT.
struct GPULightSource {
    float4 originAndType;
    float4 colorAndIntensity;
    float4 directionAndRadius;
    float4 coneAndSeedAlpha;
    float4 trueOriginVoxel;
};
constant int kLightTypeSpot = 3;
constant float kConeEdgeSoftness = 1.15f;
// Metal has no radians() builtin (GLSL does); convert degrees inline.
constant float kDegToRad = 3.14159265358979323846f / 180.0f;

// Analytic SPOT falloff at world position `pos3D` for the 0-based light
// `lightIdx`. 1.0 inside the cone, smoothstep to 0.0 across the soft edge
// band, 0.0 outside. Apex is the light's TRUE (unclamped) origin. Mirrors
// the GLSL twin.
float spotConeFactor(device const GPULightSource* lights, int lightIdx, float3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const float3 axis = normalize(L.directionAndRadius.xyz);
    const float3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4f) {
        return 1.0f;   // at the apex — fully lit, avoid a 0/0 direction.
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = L.coneAndSeedAlpha.x * 0.5f * kDegToRad;
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, 90.0f * kDegToRad));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
float3 ACESFilm(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

// Per-axis empty-cell compaction (#2256): on the per-axis route
// (perAxisRoute != 0) this kernel is dispatched indirectly over only each axis's
// OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass). compactedCells
// holds the occupied linear cell indices; cellDrawArgs carries visibleCount at
// [kDispatchArgsBaseUint + 3] for the 1-D bound guard. GLSL twin's bindings 25/26.
constant uint kDispatchArgsBaseUint = 8u;
constant uint kPerAxisCellComputeTile = 256u;

kernel void c_lighting_to_trixel(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    constant FrameDataVoxelToTrixel& voxelFrameData [[buffer(7)]],
    constant FrameDataSun& sunFrameData [[buffer(29)]],
    constant LightVolumeParams& lightVolumeParams [[buffer(23)]],
    // Baked sun-aligned depth map — read by the detached world-receive path
    // (#1576 P4b-2) to re-run the cascade lookup at a world-placed voxel's pos.
    device const uint* sunDepthBuf [[buffer(28)]],
    // Light list for SPOT cone shaping (#2318): the winning-light ID indexes
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
    // Entity-id channel (#2124 lit-cross-section follow-up): read ONLY for the fog
    // cut-face flag (bit 29, set by stage 2 / decodeCutFace). Bound at unit 6 on the
    // single-canvas + detached routes; the `perAxisRoute == 0` guard skips the read
    // on the rotation route. GLSL twin's binding 6.
    texture2d<uint, access::read> trixelEntityIds [[texture(6)]],
    // Winning-light ID volume (#2318), unit 7. `.r` = light index+1 (÷255) of
    // the flood winner per cell. Read only on the spot path; bound every tick
    // so Metal's slot table is populated. GLSL twin's binding 7.
    texture3d<float, access::read> lightVolumeId [[texture(7)]],
    // Per-axis empty-cell compaction (#2256) — GLSL twin's bindings 25/26.
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
    if (voxelFrameData.perAxisRoute != 0) {
        // #2256: indirect dispatch over the compacted occupied-cell list, folded
        // into a capped 2-D threadgroup grid by c_per_axis_cell_finalize —
        // idx = flat group index * tile + local flat index, guarded by the axis's
        // visibleCount, then decode the pixel from its linear cell.
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

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (voxelFrameData.perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    // A detached re-voxelize canvas (#1558) is lit by AO + directional sun + sky
    // only by DEFAULT; the opt-in world-placed path (#1576 P4b-2,
    // detachedWorldReceive.w != 0) instead has it RECEIVE world sun-shadow + 128³
    // light-volume bleed at its recovered world pos, like a GRID solid. Default
    // path stays byte-identical. Mirrors c_lighting_to_trixel.glsl.
    const bool detachedCanvas = voxelFrameData.isDetachedCanvas != 0.0f;
    const bool worldReceive = detachedCanvas && voxelFrameData.detachedWorldReceive.w != 0.0f;

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (#1458 per-axis / single-canvas, flip carrier #2207).
    const int rawDepth = decodeDepthRoute(encoded, voxelFrameData.perAxisRoute);
    // Decode the visible-triplet slot (#1278) → world FaceId → world-frame
    // six-face outward normal. Used by Lambert, the sky-term, and the
    // world-receive sun-shadow normal — hoisted above the shadow read.
    // The riser-polarity flip (#2207) selects the OPPOSITE same-axis face, so
    // a flipped silhouette riser shades with its true outward normal instead
    // of the inverted triplet one (the venetian near-black rows at 180°).
    const int slot = decodeSlot(encoded);
    const int faceId =
        voxelFrameData.visibleFaceIds[slot] ^ decodeFlipRoute(encoded, voxelFrameData.perAxisRoute);
    float3 worldNormal = faceOutwardNormal6(faceId);

    // Recover this voxel's WORLD position once for an opt-in world-placed
    // detached solid (model pos + the entity's world cell origin); shared by the
    // sun-shadow receive and the light-volume sample. Mirrors GLSL.
    float3 worldReceivePos = float3(0.0f);
    if (worldReceive) {
        worldReceivePos = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, voxelFrameData.trixelCanvasOffsetZ1,
            voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions,
            voxelFrameData.rasterYaw
        ) + voxelFrameData.detachedWorldReceive.xyz;
    }

    float        ao     = canvasAO.read(uint2(pixel)).r;
    // Shadow factor: the world canvas reads its COMPUTE_SUN_SHADOW result; an
    // opt-in world-placed detached solid re-runs that cascade lookup at its world
    // pos (world iso depth = model rawDepth + the offset's iso depth picks the
    // cascade); a default detached overlay stays forced fully lit.
    float shadow;
    if (worldReceive) {
        shadow = sunFrameData.shadowsEnabled != 0
            ? worldSunShadowFactor(
                  worldReceivePos, worldNormal,
                  float(rawDepth) + voxelFrameData.detachedWorldReceive.x +
                      voxelFrameData.detachedWorldReceive.y +
                      voxelFrameData.detachedWorldReceive.z,
                  sunFrameData, sunDepthBuf
              )
            : 1.0f;
    } else {
        shadow = detachedCanvas ? 1.0f : canvasSunShadow.read(uint2(pixel)).r;
    }
    const float4 src    = trixelColors.read(uint2(pixel));

    if (frameData.debugOverlayMode != 0) {
        float3 debugColor = float3(0.0f);
        if (frameData.debugOverlayMode == 1) {
            debugColor = float3(1.0f - ao, ao, 0.0f);
        } else if (frameData.debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = float3(level, level, 1.0f);
        } else {
            debugColor = shadow >= 0.999f ? float3(0.0f) : float3(1.0f, 0.0f, 1.0f);
        }
        trixelColors.write(float4(debugColor, src.a), uint2(pixel));
        return;
    }

    // Fog cross-section CUT face (#2124 lit-cross-section follow-up) — see GLSL
    // twin. The interior wall exposed at the vision boundary is geometrically
    // buried, so the sun-shadow map reports it self-shadowed and AO reads it as a
    // deep crease; force it fully lit (shadow + AO = 1) so it shades as a clean
    // exposed face. Flag rides bit 29 of the stored id; the `perAxisRoute == 0`
    // guard skips the read on the rotation route (id image unbound there).
    if (voxelFrameData.perAxisRoute == 0 &&
        decodeCutFace(trixelEntityIds.read(uint2(pixel)).xy)) {
        ao = 1.0f;
        shadow = 1.0f;
    }

    // Sun direction is world frame; worldNormal (decoded above) is the matching
    // world-frame surface normal; Lambert is a plain dot product. Mirrors GLSL.
    const float lambert = max(0.0f, dot(worldNormal, sunFrameData.sunDirection.xyz));
    // Sun-shadow darkens only the directional term; ambient is never blocked —
    // see the GLSL twin (a fully self-shadowed re-voxelize side face was
    // collapsing to pure black). Unshadowed pixels stay byte-identical.
    const float faceFactor =
        (sunFrameData.sunAmbient + (1.0f - sunFrameData.sunAmbient) * lambert * shadow) *
        sunFrameData.sunIntensity;

    float3 baseRgb;
    if (frameData.lutEnabled == 0) {
        baseRgb = src.rgb * ao * faceFactor;
    } else {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        const float  luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
        const float4 lut       = paletteLUT.sample(s, float2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * faceFactor;
    }

    // Light-volume bleed: the world / per-axis camera canvases sample the shared
    // 128³ volume; an opt-in world-placed detached solid samples it too, at its
    // recovered world pos (#1576 P4b-2). A default detached overlay stays
    // excluded — byte-identical. Mirrors GLSL.
    if (frameData.lightVolumeEnabled != 0 && (!detachedCanvas || worldReceive)) {
        // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
        // face-locally; the single canvas uses the cardinal-snap reconstruction.
        // The shared world light volume is sampled the same way for both. The
        // world-placed detached solid reuses worldReceivePos (model + offset).
        // Smooth-yaw single-canvas recovery (#1719): while rotating, the main
        // canvas's remaining SDF/text content stores at the FULL visualYaw
        // with view-frame depth, so the light-volume sample position must use
        // the smooth inverse or the glow drifts off the surface as |residual|
        // grows. residualYaw == 0 (and every detached canvas, whose frame
        // carries zero yaw) keeps the byte-identical cardinal recovery.
        // Sub-cell recovery on the per-axis route — the volume sample must
        // land on the drawn surface (see perAxisCellToWorld3DSubCell).
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

        // SPOT cone shaping (#2318). Gated on the has-SPOT flag
        // (worldOriginVoxel.w) so no-spot scenes skip the ID fetch + light-list
        // read entirely and stay byte-identical. Fetch the winning light's ID at
        // the surface voxel's own cell (NEAREST — not interpolated); if it is a
        // SPOT, attenuate its volume contribution by the analytic cone factor.
        if (lightVolumeParams.worldOriginVoxel.w != 0) {
            const int3 idCell = int3(floor(localPos + float3(kLightVolumeHalfExtent) + float3(0.5)));
            if (all(idCell >= int3(0)) && all(idCell < int3(int(kLightVolumeSize)))) {
                const int winId = int(round(lightVolumeId.read(uint3(idCell)).r * 255.0f));
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
        baseRgb = ACESFilm(baseRgb * frameData.exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0f, 1.0f);
    }

    trixelColors.write(float4(baseRgb, src.a), uint2(pixel));
}
