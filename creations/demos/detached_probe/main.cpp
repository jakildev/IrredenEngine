// detached_probe — detached-canvas orientation / clipping / placement-parity
// harness.
//
// One z-ASYMMETRIC banded totem model (8x6x16: RED feet -> GREEN shins ->
// BLUE torso -> WHITE head, plus a MAGENTA +x face marker) is spawned five
// times: two GRID world-reference anchors (identity + a 45deg-Z seed), the two
// detached flavors — DETACHED (forward-scatter) and DETACHED_REVOXELIZE — and
// a 45deg-Z GRID totem spawned WITHOUT C_RotationMode (the implicit-GRID
// regression totem, #2376). All five sit on one iso row so every capture
// frames them side by side. No lighting stack — raw albedo keeps the band
// classifier exact.
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
//   [detached-probe] DOMAIN-STATE grid-default-parity shot=<label>
//       match_vs_seeded=(ex,ey) distinct_vs_identity=(ex,ey) tol=T
//       matched=yes|no distinct=yes|no result=PASS|FAIL
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
//   * implicit-GRID DOMAIN STATE (#2376) — a totem spawned with no
//     C_RotationMode must render its authored rotation exactly like the
//     explicit-GRID seeded anchor (MATCH), and both must be measurably
//     distinguishable from the unrotated identity anchor (DISTINCT, the
//     positive control). Catches REBUILD_GRID_VOXELS_IMPLICIT going missing
//     from a pipeline, which silently drops the rotation back to identity.
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
constexpr Color kBandRed{230, 40, 40, 255};      // feet   z [0,4)
constexpr Color kBandGreen{40, 210, 60, 255};    // shins  z [4,8)
constexpr Color kBandBlue{40, 90, 230, 255};     // torso  z [8,12)
constexpr Color kBandWhite{245, 245, 245, 255};  // head   z [12,16)
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

// World placement: along the iso-horizontal axis (x = t, y = -t) so all five
// share one screen row (iso y = -x - y + 2z is t-independent). Each detached
// totem gets a GRID anchor twin at the SAME rotation, so the parity
// expectation (centroid delta == iso-projected translation delta) holds — a
// rotated solid's band centroid shifts around the model origin, so an
// unrotated anchor would mis-predict the rotated totem's centroid.
constexpr float kTotemZ = 6.0f;
constexpr vec3 kGridPos{-14.0f, 14.0f, kTotemZ};       // identity anchor
constexpr vec3 kDetachedPos{0.0f, 0.0f, kTotemZ};      // identity, avatar config
constexpr vec3 kRevoxPos{14.0f, -14.0f, kTotemZ};      // 45deg-Z seed
constexpr vec3 kGridSeededPos{-28.0f, 28.0f, kTotemZ}; // 45deg-Z anchor
// 45deg-Z again, but spawned WITHOUT C_RotationMode (#2376). Placed further
// down the -iso.x end of the row than the walk corridor reaches, NOT mirrored
// onto the +iso.x side past the seeded anchor: at zoom 4 the seeded anchor's
// bucket center already sits at ~1170 of the 1284 px framebuffer under the pan
// shot, so a totem beyond it would clip off-frame and its bands would vanish.
// The clearance this position has to hold is asserted below.
constexpr vec3 kGridImplicitPos{33.0f, -33.0f, kTotemZ};

// --walk ping-pong half-span, at namespace scope so the placement assert below
// can see it (driveWalk is the only other reader).
constexpr float kWalkSpan = 8.0f;

// Conservative iso-x extent of one totem: the unrotated box spans x [0,8) and
// y [0,6), and iso.x = -x + y, so its bands cover 14 iso units. (A 45deg-Z
// totem covers only ~8.5, so 14 bounds both poses.)
constexpr float kTotemIsoWidth =
    static_cast<float>(kTotemSize.x) + static_cast<float>(kTotemSize.y);

// runProbeAsserts buckets framebuffer pixels by NEAREST expected center, so two
// totems whose bands overlap in screen-x get misattributed between buckets —
// silently corrupting whichever asserts read them. --walk sweeps the two
// detached totems by delta{+o, -o, 0} for o in [0, kWalkSpan], i.e. by
// -2 * kWalkSpan in iso.x, so they close on whatever sits at the -iso.x end of
// the row. Only the implicit totem is stationary on that side (the identity and
// seeded anchors sit at +iso.x, which the walk moves away from), so it is the
// one placement the walk can invalidate — and it is checked here rather than
// left to a --walk run to discover, because nothing automated combines --walk
// with --probe-assert.
static_assert(
    pos3DtoPos2DIso(kRevoxPos).x - 2.0f * kWalkSpan - pos3DtoPos2DIso(kGridImplicitPos).x >=
        kTotemIsoWidth,
    "implicit-GRID totem sits inside the --walk corridor: at maximum walk offset its bands "
    "overlap the revoxelize totem's in screen-x, aliasing their nearest-center buckets. Move it "
    "further along -iso.x (mind the framebuffer width), or shorten the row -- do NOT shrink "
    "kWalkSpan, whose sub-voxel sweep length is load-bearing for fractional-position coverage."
);

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
IREntity::EntityId g_gridImplicitTotem{};
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

// The explicit-C_RotationMode spawn. Both parity anchors use it, so the
// anchors stay on the archetype REBUILD_GRID_VOXELS itself ticks — the
// measurement is then a GRID-vs-GRID comparison at a known pose, not a
// cross-arm one (see #2349 for the mis-measurement that motivates it).
IREntity::EntityId spawnGridTotem(vec3 worldPos, vec4 rotation) {
    IREntity::EntityId totem = IREntity::createEntity(
        C_LocalTransform{worldPos, rotation},
        C_RotationMode{RotationMode::GRID},
        C_VoxelSetNew{kTotemSize, kBandWhite, true}
    );
    paintTotemBands(IREntity::getComponent<C_VoxelSetNew>(totem));
    return totem;
}

// The implicit-GRID spawn (#2376): identical, minus C_RotationMode. Absence of
// the component is documented as implicitly GRID
// (`component_rotation_mode.hpp`), so this totem must rasterize its authored
// rotation exactly like `spawnGridTotem` does — that equivalence is what
// `assertImplicitGridParity` measures.
IREntity::EntityId spawnGridTotemNoRotationMode(vec3 worldPos, vec4 rotation) {
    IREntity::EntityId totem = IREntity::createEntity(
        C_LocalTransform{worldPos, rotation},
        C_VoxelSetNew{kTotemSize, kBandWhite, true}
    );
    paintTotemBands(IREntity::getComponent<C_VoxelSetNew>(totem));
    return totem;
}

IREntity::EntityId spawnDetachedTotem(
    const char *canvasName, RotationMode mode, vec3 worldPos, IREntity::EntityId &outCanvasEntity
) {
    C_EntityCanvas canvas =
        IRPrefab::EntityCanvas::createWithVoxelPool(canvasName, kProbeCanvasSize, kProbePoolSize);
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
    const vec4 seedRotation =
        mode == RotationMode::DETACHED_REVOXELIZE ? kSeedRotation : vec4(0.0f, 0.0f, 0.0f, 1.0f);
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
    const int period = static_cast<int>(2.0f * kWalkSpan / kStep);
    const int phase = (g_walkFrame - g_autoWarmupFrames) % period;
    const float t = static_cast<float>(phase) * kStep;
    const float offset = (t <= kWalkSpan) ? t : 2.0f * kWalkSpan - t; // triangle wave
    const vec3 delta{offset, -offset, 0.0f};                          // stays on the iso row

    IREntity::getComponent<C_LocalTransform>(g_detachedTotem).translation_ = kDetachedPos + delta;
    IREntity::getComponent<C_LocalTransform>(g_revoxTotem).translation_ = kRevoxPos + delta;
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
    double centroidX() const {
        return count_ > 0 ? sumX_ / count_ : 0.0;
    }
    double centroidY() const {
        return count_ > 0 ? sumY_ / count_ : 0.0;
    }
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
    std::vector<std::uint8_t> pixels(static_cast<size_t>(size.x) * static_cast<size_t>(size.y) * 4);
    texture->getSubImage2D(
        0,
        0,
        size.x,
        size.y,
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

// ---- Centroid-residual measurement (shared by both parity asserts) ----------

// Residual between a totem's MEASURED band-centroid offset from an anchor and
// the offset PREDICTED by pure iso-projected translation. A residual within
// tolerance means the pair renders at the same pose; a residual far outside it
// means the two poses differ (which is what the anchors are chosen to detect).
struct CentroidResidual {
    double measuredDx_ = 0.0;
    double measuredDy_ = 0.0;
    vec2 expectedGamePx_{0.0f};
    double errX_ = 0.0;
    double errY_ = 0.0;
};

CentroidResidual measureCentroidResidual(
    const BandStats &target,
    const BandStats &anchor,
    IREntity::EntityId targetEntity,
    IREntity::EntityId anchorEntity,
    float zoom
) {
    CentroidResidual out;
    out.measuredDx_ = target.centroidX() - anchor.centroidX();
    out.measuredDy_ = target.centroidY() - anchor.centroidY();

    const vec3 targetPos = IREntity::getComponent<C_LocalTransform>(targetEntity).translation_;
    const vec3 anchorPos = IREntity::getComponent<C_LocalTransform>(anchorEntity).translation_;
    const vec2 isoDelta = pos3DtoPos2DIso(targetPos) - pos3DtoPos2DIso(anchorPos);
    out.expectedGamePx_ = pos2DIsoToPos2DGameResolution(isoDelta, vec2(zoom));

    // The x axis convention is shared between the readback and the iso
    // projection; the y axis flips per backend, so compare |dy| only.
    out.errX_ = IRMath::abs(out.measuredDx_ - static_cast<double>(out.expectedGamePx_.x));
    out.errY_ = IRMath::abs(
        IRMath::abs(out.measuredDy_) - IRMath::abs(static_cast<double>(out.expectedGamePx_.y))
    );
    return out;
}

// First band both buckets show pixels for, preferring the head (WHITE) but
// falling back to the feet (RED) so a measurement still reports while an
// inversion bug hides the head. -1 when the two share no visible band.
int sharedBand(
    const std::vector<std::array<BandStats, kBandCount>> &fbBuckets, int bucketA, int bucketB
) {
    for (int candidate : {kWhite, kRed}) {
        if (fbBuckets[static_cast<size_t>(bucketA)][static_cast<size_t>(candidate)].count_ > 0 &&
            fbBuckets[static_cast<size_t>(bucketB)][static_cast<size_t>(candidate)].count_ > 0) {
            return candidate;
        }
    }
    return -1;
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
            shotLabel,
            canvasName
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
        shotLabel,
        canvasName,
        present,
        orientation,
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
    // identity anchor, 1 = DETACHED, 2 = REVOXELIZE, 3 = GRID seeded anchor,
    // 4 = implicit-GRID seeded totem (assertImplicitGridParity's target).
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

    for (const TotemRef &totem : totems) {
        const int band = sharedBand(fbBuckets, totem.anchorBucket_, totem.bucket_);
        if (band < 0) {
            IR_LOG_ERROR(
                "[detached-probe-parity] shot={} totem={} no shared band visible result=FAIL",
                shotLabel,
                totem.name_
            );
            g_anyProbeFailure = true;
            continue;
        }

        const CentroidResidual residual = measureCentroidResidual(
            fbBuckets[static_cast<size_t>(totem.bucket_)][static_cast<size_t>(band)],
            fbBuckets[static_cast<size_t>(totem.anchorBucket_)][static_cast<size_t>(band)],
            totem.entity_,
            totem.anchorEntity_,
            zoom
        );

        // Tolerances calibrate to the measured post-fix residuals so any
        // regression toward the fixed bug classes (whole-texel anchor drift,
        // camera-offset leaks — tens of px, and the half-cell rotation-anchor
        // shift of the revox inverse resample, ~4*zoom px in x — the #2349
        // fix) trips loudly: both detached flavors now track their GRID twin
        // within the band-centroid lattice quantization + sub-texel snap bound
        // (~1.3*zoom px). The revox pair carries one extra DOCUMENTED term on
        // y only: GRID forward-rounds the totem's half-integer z coordinates
        // UP half a cell onto its integer lattice, while the anchored revox
        // fill places the content exactly (matching revox's own identity
        // convention) — a bounded cross-MODE quantization difference
        // (measured ~2.4*zoom px), not a revox placement error.
        const bool isRevox = totem.bucket_ == 2;
        const double toleranceX = 1.5 + 1.8 * zoom;
        const double toleranceY = isRevox ? 1.5 + 2.8 * zoom : toleranceX;
        const bool pass = residual.errX_ <= toleranceX && residual.errY_ <= toleranceY;
        if (!pass) {
            g_anyProbeFailure = true;
        }
        IR_LOG_INFO(
            "[detached-probe-parity] shot={} totem={} band={} measured=({:.1f},{:.1f}) "
            "expected=({:.1f},{:.1f}) tol=({:.1f},{:.1f}) result={}",
            shotLabel,
            totem.name_,
            kBandNames[band],
            residual.measuredDx_,
            residual.measuredDy_,
            residual.expectedGamePx_.x,
            residual.expectedGamePx_.y,
            toleranceX,
            toleranceY,
            pass ? "PASS" : "FAIL"
        );
    }
}

// ---- Implicit-GRID domain state (#2376) -------------------------------------

// An entity with NO C_RotationMode is documented as implicitly GRID, so a
// seeded totem spawned without the component must rasterize its authored
// 45deg-Z rotation exactly like the explicit-GRID seeded anchor does. Two
// measurements against the SAME totem, both required to pass:
//
//   MATCH   — vs the explicit-GRID SEEDED anchor (bucket 3, same 45deg pose):
//             residual within tolerance. This is the property under test.
//   DISTINCT — vs the explicit-GRID IDENTITY anchor (bucket 0, unrotated):
//             residual OUTSIDE tolerance. This is the positive control: the
//             45deg pose shifts the carved head band's centroid around the
//             model origin by a measurable amount, so it proves the
//             measurement can tell the two poses apart. Without it, MATCH
//             alone would also pass if the readback had gone blind.
//
// Both flip together on a regression: if REBUILD_GRID_VOXELS_IMPLICIT stops
// ticking this totem it renders at identity, MATCH deviates by the rotation
// shift and DISTINCT collapses into tolerance. Measured by dropping the twin
// from the pipeline: the two residual columns swap exactly, MATCH going to
// (3.9, 7.8, 15.5) px at zoom (1, 2, 4) and DISTINCT to (0, 0).
//
// Margins (measured, macOS/Metal): the 45deg yaw shifts the carved head band's
// centroid by ~3.9*zoom px against a tolerance of 1.5 + 1.8*zoom, so the
// DISTINCT control clears by 18% at zoom 1 and 78% at zoom 4 — the fixed 1.5 px
// term is what narrows it at the low end. A DISTINCT failure at zoom 1 alone,
// with zoom 2 / 4 still distinct, means the margin drifted rather than the
// implicit arm breaking; widen the pose (a rotation with a larger centroid
// shift, plus a matching explicit anchor) instead of loosening the tolerance.
void assertImplicitGridParity(
    const char *shotLabel,
    float zoom,
    const std::vector<std::array<BandStats, kBandCount>> &fbBuckets
) {
    constexpr int kIdentityAnchorBucket = 0;
    constexpr int kSeededAnchorBucket = 3;
    constexpr int kImplicitBucket = 4;

    const int matchBand = sharedBand(fbBuckets, kSeededAnchorBucket, kImplicitBucket);
    const int distinctBand = sharedBand(fbBuckets, kIdentityAnchorBucket, kImplicitBucket);
    if (matchBand < 0 || distinctBand < 0) {
        IR_LOG_ERROR(
            "[detached-probe] DOMAIN-STATE grid-default-parity shot={} "
            "no shared band visible (match={} distinct={}) result=FAIL",
            shotLabel,
            matchBand,
            distinctBand
        );
        g_anyProbeFailure = true;
        return;
    }

    const CentroidResidual match = measureCentroidResidual(
        fbBuckets[static_cast<size_t>(kImplicitBucket)][static_cast<size_t>(matchBand)],
        fbBuckets[static_cast<size_t>(kSeededAnchorBucket)][static_cast<size_t>(matchBand)],
        g_gridImplicitTotem,
        g_gridSeededTotem,
        zoom
    );
    const CentroidResidual distinct = measureCentroidResidual(
        fbBuckets[static_cast<size_t>(kImplicitBucket)][static_cast<size_t>(distinctBand)],
        fbBuckets[static_cast<size_t>(kIdentityAnchorBucket)][static_cast<size_t>(distinctBand)],
        g_gridImplicitTotem,
        g_gridTotem,
        zoom
    );

    // Same GRID-vs-GRID lattice bound the detached pair uses — both sides of
    // the MATCH comparison are world-pool totems on the identical arm, so no
    // cross-mode term applies.
    const double tolerance = 1.5 + 1.8 * zoom;
    const bool matched = match.errX_ <= tolerance && match.errY_ <= tolerance;
    const bool distinctEnough = distinct.errX_ > tolerance || distinct.errY_ > tolerance;
    const bool pass = matched && distinctEnough;
    if (!pass) {
        g_anyProbeFailure = true;
    }
    IR_LOG_INFO(
        "[detached-probe] DOMAIN-STATE grid-default-parity shot={} "
        "match_vs_seeded=({:.1f},{:.1f}) distinct_vs_identity=({:.1f},{:.1f}) tol={:.1f} "
        "matched={} distinct={} result={}",
        shotLabel,
        match.errX_,
        match.errY_,
        distinct.errX_,
        distinct.errY_,
        tolerance,
        matched ? "yes" : "no",
        distinctEnough ? "yes" : "no",
        pass ? "PASS" : "FAIL"
    );
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
    const vec3 positions[5]{
        IREntity::getComponent<C_LocalTransform>(g_gridTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_detachedTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_revoxTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_gridSeededTotem).translation_,
        IREntity::getComponent<C_LocalTransform>(g_gridImplicitTotem).translation_,
    };
    std::vector<double> centers;
    centers.reserve(5);
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

    // 4) Implicit-GRID domain state: a component-less totem renders its
    //    authored rotation like the explicit-GRID one (#2376).
    assertImplicitGridParity(shot.label_, shot.zoom_, fbBuckets);
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IREngine::args().flag(
        "--probe-assert",
        "Read back canvases on each capture and assert "
        "band presence / orientation / placement parity"
    );
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
            IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS_IMPLICIT>(),
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
    g_gridImplicitTotem = spawnGridTotemNoRotationMode(kGridImplicitPos, kSeedRotation);
    g_detachedTotem = spawnDetachedTotem(
        "probe_detached",
        RotationMode::DETACHED,
        kDetachedPos,
        g_detachedCanvas
    );
    g_revoxTotem = spawnDetachedTotem(
        "probe_revox",
        RotationMode::DETACHED_REVOXELIZE,
        kRevoxPos,
        g_revoxCanvas
    );
}
