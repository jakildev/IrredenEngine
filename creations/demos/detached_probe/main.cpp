// detached_probe — detached-canvas orientation / clipping / placement-parity
// harness.
//
// One z-ASYMMETRIC banded totem model (8x6x16: RED feet -> GREEN shins ->
// BLUE torso -> WHITE head, plus a MAGENTA +x face marker) is spawned three
// times, once per rotation mode: GRID (the world reference), DETACHED
// (forward-scatter), and DETACHED_REVOXELIZE. The three sit on one iso row so
// every capture frames them side by side. No lighting stack — raw albedo keeps
// the band classifier exact.
//
// Every prior detached-canvas canary (canvas_stress cubes / spheres /
// octahedra) is z-symmetric, so a vertical inversion of detached content
// renders byte-identically and passes reference comparison. The banded totem
// is deliberately the simplest solid that CANNOT hide that class of bug.
//
// --probe-assert: on each settled capture frame (AutoScreenshotConfig::
// onCaptureFrame_), read back the two detached canvas textures + the main
// canvas texture and emit machine-readable verdict lines:
//
//   [detached-probe-assert] shot=<label> canvas=<name> bands=N/4
//       orientation=UPRIGHT|INVERTED|UNKNOWN clipped=<list|none> result=PASS|FAIL
//   [detached-probe-parity] shot=<label> totem=<name> band=<band>
//       measured=(dx,dy) expected=(dx,dy) tol=T result=PASS|FAIL
//
//   * band PRESENCE — all four z-bands rasterized into the detached canvas.
//     A missing WHITE head band means the canvas/lattice clipped the model
//     (the plain-DETACHED zoom-overflow class: #1570-D2 caps re-voxelize only).
//   * band ORDER — the detached canvas's RED->WHITE order along canvas-y must
//     match the GRID reference's order on the main canvas. Catches detached
//     content rendering z-inverted.
//   * placement PARITY — the band centroid delta between the GRID totem and
//     each detached totem on the shared framebuffer must match the
//     iso-projected world delta at the shot's zoom. Catches composite desync
//     under camera zoom + fractional pan (the #1883 half-texel snap bound is
//     the current tolerance; tighten when the composite tracks sub-texel).
//
// --walk: advance the two detached totems along the iso row in sub-voxel steps
// per render frame (fog_demo's render-front idiom; the GRID totem stays put as
// the parity anchor), so captures exercise MOVING placement.
//
// Exits nonzero when any probe assert failed, so a headless CI run red-flags
// without image diffing.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/face_occupancy.hpp>

// PREFABS
#include <irreden/render/entity_canvas.hpp>
#include <irreden/render/camera_controls.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

#include <array>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace {

// ---- Totem model ---------------------------------------------------------

constexpr ivec3 kTotemSize{8, 6, 16}; // x forward, y lateral, z up

// Band palette. Saturated primaries so the readback classifier survives the
// raster's per-face shading (classification is by channel RATIO, not value).
constexpr Color kBandRed{230, 40, 40, 255};     // feet   z [0,4)
constexpr Color kBandGreen{40, 210, 60, 255};   // shins  z [4,8)
constexpr Color kBandBlue{40, 90, 230, 255};    // torso  z [8,12)
constexpr Color kBandWhite{245, 245, 245, 255}; // head   z [12,16)
constexpr Color kBandMagenta{230, 40, 230, 255}; // +x face marker on the torso

enum BandId : int {
    kRed = 0,
    kGreen = 1,
    kBlue = 2,
    kWhite = 3,
    kMagenta = 4,
    kBandCount = 5,
};
constexpr const char *kBandNames[kBandCount]{"RED", "GREEN", "BLUE", "WHITE", "MAGENTA"};

// World placement: along the iso-horizontal axis (x = t, y = -t) so all four
// share one screen row (iso y = -x - y + 2z is t-independent). Each detached
// totem gets a GRID anchor twin at the SAME rotation, so the parity
// expectation (centroid delta == iso-projected translation delta) holds — a
// rotated solid's band centroid shifts around the model origin, so an
// unrotated anchor would mis-predict the rotated totem's centroid.
constexpr float kTotemZ = 6.0f;
constexpr vec3 kGridPos{-14.0f, 14.0f, kTotemZ};      // identity anchor
constexpr vec3 kDetachedPos{0.0f, 0.0f, kTotemZ};     // identity, avatar config
constexpr vec3 kRevoxPos{14.0f, -14.0f, kTotemZ};     // 45deg-Z seed
constexpr vec3 kGridSeededPos{-28.0f, 28.0f, kTotemZ}; // 45deg-Z anchor

// Detached canvas + pool sized to mirror the smallest real consumer (a
// player-avatar canvas), so the probe also guards that exact configuration.
constexpr ivec2 kProbeCanvasSize{128, 128};
constexpr ivec3 kProbePoolSize{16, 16, 20};

// ---- Shots ---------------------------------------------------------------

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0.0f, 0.0f), 0.0f, "probe_z1_cam00"},
    {2.0f, vec2(0.0f, 0.0f), 0.0f, "probe_z2_cam00"},
    {4.0f, vec2(0.0f, 0.0f), 0.0f, "probe_z4_cam00"},
    // Fractional camera offsets: the sub-pixel decomposition / composite snap
    // path. The parity tolerance widens to the documented half-texel bound.
    {2.0f, vec2(0.5f, 0.25f), 0.0f, "probe_z2_camfrac"},
    {4.0f, vec2(0.5f, 0.25f), 0.0f, "probe_z4_camfrac"},
    // Off-origin pan: detached canvases must survive a panned camera
    // (the #1555 cull-on-pan class) and stay aligned with the world.
    {4.0f, vec2(10.4f, -6.7f), 0.0f, "probe_z4_campan"},
};

// ---- Globals (spawn-time ids the probe + walk hooks read) -----------------

bool g_probeAssert = false;
bool g_walk = false;
int g_autoWarmupFrames = 0;
int g_walkFrame = 0;
bool g_anyProbeFailure = false;

IREntity::EntityId g_gridTotem{};
IREntity::EntityId g_gridSeededTotem{};
IREntity::EntityId g_detachedTotem{};
IREntity::EntityId g_revoxTotem{};
IREntity::EntityId g_detachedCanvas{};
IREntity::EntityId g_revoxCanvas{};

// The canvas_stress orbiter "off-cardinal seed" pose, shared by the
// re-voxelize totem and its GRID anchor twin. A Z yaw preserves the z band
// stacking, so the band-order assert stays valid under the seed.
const vec4 kSeedRotation = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kQuarterPi);

// ---- Model paint ----------------------------------------------------------

// Band + carve rule shared by both paint idioms below. Returns false when the
// voxel is carved away (the head band narrows to an off-center block, so the
// SILHOUETTE is z-asymmetric — a full box only exposes inversion via colors).
//
// World convention: +z points DOWN on screen ("up" is -z; compare
// canvas_stress's kFloorZ sitting BELOW cube bottoms at a LARGER z). The head
// band therefore lives at the LOW-z end of the centered span.
bool totemBandColor(vec3 p, vec3 half, Color &outColor) {
    const float zFromFeet = half.z - p.z; // 0 feet (world-down) .. 16 head (world-up)
    outColor = kBandRed;
    if (zFromFeet >= 12.0f) {
        outColor = kBandWhite;
        if (IRMath::abs(p.y) > half.y * 0.6f || p.x < -half.x * 0.2f) {
            return false; // carved
        }
    } else if (zFromFeet >= 8.0f) {
        outColor = kBandBlue;
        if (p.x > half.x - 1.5f) {
            outColor = kBandMagenta; // +x face marker
        }
    } else if (zFromFeet >= 4.0f) {
        outColor = kBandGreen;
    }
    return true;
}

// Canonical mutator path: editVoxels applies the edit AND resyncs every piece
// of derived state (rotation-source mirror -> active mask -> face occupancy).
void paintTotemBands(C_VoxelSetNew &voxelSet) {
    const vec3 half = vec3(voxelSet.size_) * 0.5f;
    voxelSet.editVoxels([&](int, C_Voxel &voxel, vec3 p) {
        Color color;
        if (!totemBandColor(p, half, color)) {
            voxel.deactivate();
            return;
        }
        voxel.color_ = color;
    });
}

// The RAW carve idiom several creations still use (raw span writes +
// syncActiveMask + recomputeFaceOccupancy, no editVoxels): kept here
// deliberately so the probe exercises the same derived-state path a
// raw-carved detached avatar takes.
void paintTotemBandsRawIdiom(C_VoxelSetNew &voxelSet) {
    const vec3 half = vec3(voxelSet.size_) * 0.5f;
    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        const vec3 p = voxelSet.positions_[i].pos_;
        Color color;
        if (!totemBandColor(p, half, color)) {
            voxelSet.voxels_[i].deactivate();
            continue;
        }
        voxelSet.voxels_[i].color_ = color;
    }
    voxelSet.syncActiveMask();
    IRPrefab::Voxel::recomputeFaceOccupancy(voxelSet.voxels_, voxelSet.size_);
}

// ---- Spawns ---------------------------------------------------------------

IREntity::EntityId spawnGridTotem(vec3 worldPos, vec4 rotation) {
    IREntity::EntityId totem = IREntity::createEntity(
        C_LocalTransform{worldPos, rotation},
        C_VoxelSetNew{kTotemSize, kBandWhite, true}
    );
    paintTotemBands(IREntity::getComponent<C_VoxelSetNew>(totem));
    return totem;
}

IREntity::EntityId spawnDetachedTotem(
    const char *canvasName,
    RotationMode mode,
    vec3 worldPos,
    IREntity::EntityId &outCanvasEntity
) {
    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        canvasName, kProbeCanvasSize, kProbePoolSize
    );
    canvas.screenLocked_ = false; // world-placed: the game-avatar configuration
    outCanvasEntity = canvas.canvasEntity_;

    IREntity::EntityId geom = IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{kTotemSize, kBandWhite, true, canvas.canvasEntity_}
    );
    // The plain-DETACHED totem carves via the RAW idiom, the re-voxelize one
    // via editVoxels, so the probe covers both derived-state resync paths.
    if (mode == RotationMode::DETACHED) {
        paintTotemBandsRawIdiom(IREntity::getComponent<C_VoxelSetNew>(geom));
    } else {
        paintTotemBands(IREntity::getComponent<C_VoxelSetNew>(geom));
    }

    // The plain-DETACHED totem stays at identity (the walking-avatar
    // configuration); the re-voxelize one seeds the off-cardinal pose so the
    // probe also covers the real rotation bake.
    const vec4 seedRotation = mode == RotationMode::DETACHED_REVOXELIZE
        ? kSeedRotation
        : vec4(0.0f, 0.0f, 0.0f, 1.0f);
    IREntity::EntityId totem = IREntity::createEntity(
        C_LocalTransform{worldPos, seedRotation},
        C_RotationMode{mode},
        canvas
    );
    // Sub-trixel placement so --walk exercises fractional world positions.
    IREntity::getComponent<C_LocalTransform>(totem).unbounded_ = true;
    return totem;
}

// ---- Walk hook (render-front, fog_demo idiom) ------------------------------

// Ping-pong the two detached totems along the iso row in sub-voxel steps. The
// GRID totem never moves: it is the parity anchor every measurement compares
// against. Held through warmup so shot 0 starts at the authored pose.
void driveWalk() {
    ++g_walkFrame;
    if (g_walkFrame <= g_autoWarmupFrames) {
        return;
    }
    constexpr float kStep = 0.15f; // world units per render frame, sub-voxel
    constexpr float kSpan = 8.0f;  // ping-pong half-span around the spawn pose
    const int period = static_cast<int>(2.0f * kSpan / kStep);
    const int phase = (g_walkFrame - g_autoWarmupFrames) % period;
    const float t = static_cast<float>(phase) * kStep;
    const float offset = (t <= kSpan) ? t : 2.0f * kSpan - t; // triangle wave
    const vec3 delta{offset, -offset, 0.0f}; // stays on the iso row

    IREntity::getComponent<C_LocalTransform>(g_detachedTotem).translation_ =
        kDetachedPos + delta;
    IREntity::getComponent<C_LocalTransform>(g_revoxTotem).translation_ =
        kRevoxPos + delta;
}

// ---- Readback + classification ---------------------------------------------

// Channel-RATIO classifier: the raster shades each iso face differently, so
// absolute values vary but the dominant-channel pattern survives.
int classifyBand(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    if (a == 0) {
        return -1; // cleared canvas texel
    }
    const int ri = r, gi = g, bi = b;
    const int maxC = IRMath::max(ri, IRMath::max(gi, bi));
    const int minC = IRMath::min(ri, IRMath::min(gi, bi));
    if (maxC < 40) {
        return -1; // background / too dark to attribute
    }
    if (minC * 10 > maxC * 7) {
        return kWhite; // near-neutral and bright enough
    }
    const bool rDom = ri * 10 > maxC * 8;
    const bool gDom = gi * 10 > maxC * 8;
    const bool bDom = bi * 10 > maxC * 8;
    if (rDom && bDom && !gDom) {
        return kMagenta;
    }
    if (rDom && !gDom && !bDom) {
        return kRed;
    }
    if (gDom && !rDom && !bDom) {
        return kGreen;
    }
    if (bDom && !rDom && !gDom) {
        return kBlue;
    }
    return -1;
}

struct BandStats {
    double sumX_ = 0.0;
    double sumY_ = 0.0;
    int count_ = 0;

    void add(int x, int y) {
        sumX_ += x;
        sumY_ += y;
        ++count_;
    }
    double centroidX() const { return count_ > 0 ? sumX_ / count_ : 0.0; }
    double centroidY() const { return count_ > 0 ? sumY_ / count_ : 0.0; }
};

// Read back one RGBA8 texture and accumulate per-band stats over the whole
// image (optionally bucketing by nearest expected-x when `bucketCenters` is
// non-empty — used for the shared framebuffer where all three totems coexist).
void accumulateBands(
    const IRRender::Texture2D *texture,
    ivec2 size,
    const std::vector<double> &bucketCenters,
    std::vector<std::array<BandStats, kBandCount>> &outBuckets
) {
    std::vector<std::uint8_t> pixels(
        static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4
    );
    texture->getSubImage2D(
        0, 0, size.x, size.y,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        pixels.data()
    );

    const int numBuckets = bucketCenters.empty() ? 1 : static_cast<int>(bucketCenters.size());
    outBuckets.assign(numBuckets, {});

    for (int y = 0; y < size.y; ++y) {
        for (int x = 0; x < size.x; ++x) {
            const size_t i = (static_cast<size_t>(y) * size.x + x) * 4;
            const int band = classifyBand(pixels[i], pixels[i + 1], pixels[i + 2], pixels[i + 3]);
            if (band < 0) {
                continue;
            }
            int bucket = 0;
            if (!bucketCenters.empty()) {
                double best = -1.0;
                for (int c = 0; c < numBuckets; ++c) {
                    const double d = IRMath::abs(static_cast<double>(x) - bucketCenters[c]);
                    if (best < 0.0 || d < best) {
                        best = d;
                        bucket = c;
                    }
                }
            }
            outBuckets[static_cast<size_t>(bucket)][static_cast<size_t>(band)].add(x, y);
        }
    }
}

// Orientation sign along texture y: +1 when the WHITE head band sits at larger
// y than the RED feet band, -1 for the reverse, 0 when either band is absent.
int orientationSign(const std::array<BandStats, kBandCount> &bands) {
    if (bands[kWhite].count_ == 0 || bands[kRed].count_ == 0) {
        return 0;
    }
    return bands[kWhite].centroidY() > bands[kRed].centroidY() ? 1 : -1;
}

// ---- The per-shot probe -----------------------------------------------------

void assertDetachedCanvas(
    const char *shotLabel,
    const char *canvasName,
    IREntity::EntityId canvasEntity,
    int gridOrientation
) {
    auto texturesOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(canvasEntity);
    if (!texturesOpt.has_value()) {
        IR_LOG_ERROR(
            "[detached-probe-assert] shot={} canvas={} missing textures result=FAIL",
            shotLabel, canvasName
        );
        g_anyProbeFailure = true;
        return;
    }
    const C_TriangleCanvasTextures &textures = *texturesOpt.value();

    std::vector<std::array<BandStats, kBandCount>> buckets;
    accumulateBands(textures.getTextureColors(), textures.size_, {}, buckets);
    const std::array<BandStats, kBandCount> &bands = buckets[0];

    int present = 0;
    std::string clipped;
    for (int b = kRed; b <= kWhite; ++b) {
        if (bands[static_cast<size_t>(b)].count_ > 0) {
            ++present;
        } else {
            if (!clipped.empty()) {
                clipped += ",";
            }
            clipped += kBandNames[b];
        }
    }

    const int sign = orientationSign(bands);
    const char *orientation = "UNKNOWN";
    if (sign != 0 && gridOrientation != 0) {
        orientation = (sign == gridOrientation) ? "UPRIGHT" : "INVERTED";
    }

    const bool pass = present == 4 && gridOrientation != 0 && sign == gridOrientation;
    if (!pass) {
        g_anyProbeFailure = true;
    }
    IR_LOG_INFO(
        "[detached-probe-assert] shot={} canvas={} bands={}/4 orientation={} clipped={} result={}",
        shotLabel, canvasName, present, orientation,
        clipped.empty() ? "none" : clipped.c_str(),
        pass ? "PASS" : "FAIL"
    );
}

void assertPlacementParity(
    const char *shotLabel,
    float zoom,
    const std::vector<std::array<BandStats, kBandCount>> &fbBuckets
) {
    // Buckets follow the positions[] order in runProbeAsserts: 0 = GRID
    // identity anchor, 1 = DETACHED, 2 = REVOXELIZE, 3 = GRID seeded anchor.
    // Each detached totem compares against the anchor at ITS rotation, so the
    // centroid delta is a pure translation delta.
    struct TotemRef {
        const char *name_;
        IREntity::EntityId entity_;
        int bucket_;
        IREntity::EntityId anchorEntity_;
        int anchorBucket_;
    };
    const TotemRef totems[2]{
        {"detached", g_detachedTotem, 1, g_gridTotem, 0},
        {"revox", g_revoxTotem, 2, g_gridSeededTotem, 3},
    };

    // Parity band: prefer the head (WHITE) but fall back to the feet (RED) so
    // the measurement still reports while an inversion bug hides the head.
    for (const TotemRef &totem : totems) {
        int band = -1;
        for (int candidate : {kWhite, kRed}) {
            if (fbBuckets[static_cast<size_t>(totem.anchorBucket_)][static_cast<size_t>(candidate)]
                        .count_ > 0 &&
                fbBuckets[static_cast<size_t>(totem.bucket_)][static_cast<size_t>(candidate)]
                        .count_ > 0) {
                band = candidate;
                break;
            }
        }
        if (band < 0) {
            IR_LOG_ERROR(
                "[detached-probe-parity] shot={} totem={} no shared band visible result=FAIL",
                shotLabel, totem.name_
            );
            g_anyProbeFailure = true;
            continue;
        }

        const BandStats &anchor =
            fbBuckets[static_cast<size_t>(totem.anchorBucket_)][static_cast<size_t>(band)];
        const BandStats &target = fbBuckets[static_cast<size_t>(totem.bucket_)][static_cast<size_t>(band)];
        const double measuredDx = target.centroidX() - anchor.centroidX();
        const double measuredDy = target.centroidY() - anchor.centroidY();

        const vec3 totemPos = IREntity::getComponent<C_LocalTransform>(totem.entity_).translation_;
        const vec3 anchorPos =
            IREntity::getComponent<C_LocalTransform>(totem.anchorEntity_).translation_;
        const vec2 isoDelta = pos3DtoPos2DIso(totemPos) - pos3DtoPos2DIso(anchorPos);
        const vec2 expectedGamePx = pos2DIsoToPos2DGameResolution(isoDelta, vec2(zoom));

        // The x axis convention is shared between the readback and the iso
        // projection; the y axis flips per backend, so compare |dy| only.
        // Tolerances calibrate to the measured post-fix residuals so any
        // regression toward the fixed bug classes (whole-texel anchor drift,
        // camera-offset leaks — tens of px) trips loudly:
        //   * plain DETACHED tracks its GRID twin within ~1.3*zoom px
        //     (band-centroid lattice quantization + sub-texel snap);
        //   * DETACHED_REVOXELIZE additionally carries a ~1.9-iso-px
        //     rotation-anchor displacement from the pool re-fill (the rebuild
        //     rotates about a center offset from the model origin) — a real,
        //     separately-tracked residual; tighten when that lands.
        const bool isRevox = totem.bucket_ == 2;
        const double tolerance = isRevox ? 1.5 + 4.7 * zoom : 1.5 + 1.8 * zoom;
        const double errX = IRMath::abs(measuredDx - static_cast<double>(expectedGamePx.x));
        const double errY =
            IRMath::abs(IRMath::abs(measuredDy) - IRMath::abs(static_cast<double>(expectedGamePx.y)));
        const bool pass = errX <= tolerance && errY <= tolerance;
        if (!pass) {
            g_anyProbeFailure = true;
        }
        IR_LOG_INFO(
            "[detached-probe-parity] shot={} totem={} band={} measured=({:.1f},{:.1f}) "
            "expected=({:.1f},{:.1f}) tol={:.1f} result={}",
            shotLabel, totem.name_, kBandNames[band], measuredDx, measuredDy,
            expectedGamePx.x, expectedGamePx.y, tolerance, pass ? "PASS" : "FAIL"
        );
    }
}

void runProbeAsserts(int shotIndex) {
    if (!g_probeAssert) {
        return;
    }
    const IRVideo::AutoScreenshotShot &shot = kShots[shotIndex];

    // 1) Reference orientation from the GRID totem on the main canvas.
    const IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    auto mainTexturesOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvas);
    int gridOrientation = 0;
    if (mainTexturesOpt.has_value()) {
        const C_TriangleCanvasTextures &mainTextures = *mainTexturesOpt.value();
        std::vector<std::array<BandStats, kBandCount>> buckets;
        accumulateBands(mainTextures.getTextureColors(), mainTextures.size_, {}, buckets);
        gridOrientation = orientationSign(buckets[0]);
    }
    if (gridOrientation == 0) {
        IR_LOG_ERROR(
            "[detached-probe-assert] shot={} GRID reference bands missing on main canvas "
            "result=FAIL",
            shot.label_
        );
        g_anyProbeFailure = true;
    }

    // 2) Orientation + clipping per detached canvas (canvas-space readback,
    //    independent of composite placement and depth).
    assertDetachedCanvas(shot.label_, "probe_detached", g_detachedCanvas, gridOrientation);
    assertDetachedCanvas(shot.label_, "probe_revox", g_revoxCanvas, gridOrientation);

    // 3) Placement parity on the shared framebuffer (post-composite).
    auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
    const ivec2 fbSize = framebuffer.getResolutionPlusBuffer();
    // Bucket band pixels by nearest expected screen-x per totem. Iso-x maps to
    // game px at (2 * zoom) px per iso px about the framebuffer center; the few
    // px of camera sub-pixel offset are far below the totem spacing.
    const vec3 positions[4]{
        IREntity::getComponent<C_LocalTransform>(g_gridTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_detachedTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_revoxTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_gridSeededTotem).translation_,
    };
    std::vector<double> centers;
    centers.reserve(4);
    for (const vec3 &position : positions) {
        // World content lands at `originZ1 + floor(cameraIso) + iso(P)` on the
        // main canvas (the raster's +frameCanvasOffset convention), so the
        // camera term ADDS to the expected screen x.
        const vec2 iso = pos3DtoPos2DIso(position) + IRMath::floor(shot.cameraIso_);
        const vec2 gamePx = pos2DIsoToPos2DGameResolution(iso, vec2(shot.zoom_));
        centers.push_back(static_cast<double>(fbSize.x) * 0.5 + gamePx.x);
    }
    std::vector<std::array<BandStats, kBandCount>> fbBuckets;
    accumulateBands(&framebuffer.getTextureColor(), fbSize, centers, fbBuckets);

    assertPlacementParity(shot.label_, shot.zoom_, fbBuckets);
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IREngine::args().flag("--probe-assert", "Read back canvases on each capture and assert "
                                            "band presence / orientation / placement parity");
    IREngine::args().flag("--walk", "Ping-pong the detached totems along the iso row");
    IR_LOG_INFO("Starting creation: detached_probe");
    IREngine::init(argc, argv);
    g_probeAssert = IREngine::args().getFlag("--probe-assert");
    g_walk = IREngine::args().getFlag("--walk");
    g_autoWarmupFrames = IREngine::args().autoScreenshotWarmupFrames();

    initSystems();
    initCommands();
    initEntities();

    IRRender::setCameraZoom(2.0f);
    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));

    IREngine::gameLoop();

    if (g_anyProbeFailure) {
        IR_LOG_ERROR("[detached-probe] one or more probe asserts FAILED");
        return 1;
    }
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {
            IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
            IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
            IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
            IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>(),
            IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>(),
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    // Walk must advance per render frame, in lockstep with the capture counter
    // (fog_demo's render-front idiom), not on the wall-clock UPDATE step.
    if (g_walk) {
        IRSystem::SystemId walkTickId = IRSystem::createSystem<C_Name>(
            "DetachedProbeWalkTick",
            [](C_Name &) {},
            []() { driveWalk(); }
        );
        renderPipeline.push_front(walkTickId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        cfg.onCaptureFrame_ = &runProbeAsserts;
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
}

void initEntities() {
    g_gridTotem = spawnGridTotem(kGridPos, vec4(0.0f, 0.0f, 0.0f, 1.0f));
    g_gridSeededTotem = spawnGridTotem(kGridSeededPos, kSeedRotation);
    g_detachedTotem = spawnDetachedTotem(
        "probe_detached", RotationMode::DETACHED, kDetachedPos, g_detachedCanvas
    );
    g_revoxTotem = spawnDetachedTotem(
        "probe_revox", RotationMode::DETACHED_REVOXELIZE, kRevoxPos, g_revoxCanvas
    );
}
