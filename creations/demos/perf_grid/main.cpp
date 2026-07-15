#include <irreden/ir_args.hpp>
#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/profile/scope_timer.hpp>
#include <irreden/render/camera.hpp>

// Components
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_distance_hiz.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/depth_probe.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_fog_to_trixel.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/common/systems/system_modifier_decay.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_periodic_idle_position_offset.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Command suites
#include <irreden/common/command_suite_capture.hpp>

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// IRPerfGrid stress test: voxel_set vs sdf share the same lattice (positions,
// colors, periodic-idle wave). They are *not* lighting-equivalent today:
// BUILD_LIGHT_OCCLUSION_GRID only walks the voxel pool; sdf mode never allocates
// pool voxels, so the occupancy bitfield stays empty and AO + GPU light
// propagation see no occluders. Expect sdf to look brighter / less creased
// than voxel_set until Phase 3 (#428 AO via trixelDistances, #364 SDF in LOS).
// See docs/perf/metal_perf_grid_baseline.md § "IRPerfGrid mode parity".

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

enum class PerfGridMode {
    VoxelSet,
    Sdf,
    // T-287 / B1 verification modes: allocate ONE `C_VoxelSetNew` of
    // size `grid_size`³ instead of per-cell entities, then drive the
    // active-slot mask either dense (all on) or hollow (sphere shell only).
    // Use these together to measure the visibility-compaction cost as a
    // function of active-voxel count while pool-occupancy stays constant.
    DenseSet,
    HollowSet,
    // Rotated-solidity validation gallery: a static scene of ISOLATED
    // multi-voxel entities (varied dimensions + an L-shape), one row at
    // integer positions and one at fractional offsets, plus minimal
    // touching / staircase-contact pairs. Nothing stacks, so every face
    // misdraw under yaw is attributable to a single entity or a single
    // two-entity contact — the reduction harness for the per-axis
    // face-alignment class (#2411).
    Gallery,
};

// Wave motion shape for the per-entity modes (voxel_set / sdf).
//   PerCell — the default: phase advances with (x+y+z), so a z-axis
//             oscillation travels through the volume as a visible wave.
//             Neighboring cells shear apart by up to 2·a·sin(π/λ) ≈
//             0.196·a (λ = 32 at grid_size 64), so amplitudes at or below
//             λ/(2π) ≈ 5.09 keep every adjacent rounded-cell step ≤ 1 and
//             the block stays watertight — the wave reads on the surface
//             without opening see-through slits. Larger amplitudes tear
//             transient gaps open (a deliberate scatter stress, not a bug).
//   Rigid   — every cell shares phase 0, so the grid glides as one block
//             along the screen-right iso axis (world (1,-1,0)). The
//             always-touching lattice is the solidity / occupancy-cull
//             reference. Per-entity update cost matches PerCell exactly
//             (each entity still ticks its own C_PeriodicIdle and
//             re-uploads its position every frame).
enum class WaveMode {
    Rigid,
    PerCell,
};

struct PerfGridSettings {
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    int gridSize_ = 64;
    // 1.0 = contiguous 1³ voxels → a solid grid_size³ block. Values > 1
    // open (spacing - 1) units of air between cells: at 2.0 the scene is a
    // 1/8-density lattice that reads as hollow under yaw and striped at
    // cardinals (configs/perf/sparse_lattice.lua keeps that scene).
    float spacing_ = 1.0f;
    WaveMode waveMode_ = WaveMode::PerCell;
    // 5.0 sits just under the per-cell tear threshold λ/(2π) ≈ 5.09 (see the
    // WaveMode note): the traveling wave stays fully visible while every
    // voxel remains on screen at every frame.
    float waveAmplitude_ = 5.0f;
    float wavePeriodSeconds_ = 4.0f;
    bool waveOffscreen_ = false;
    float initialZoom_ = 0.5f;
    float initialYaw_ = 0.0f;
    // Subdivision — config/preset-settable; CLI overrides both.
    // Only applied if explicit_ is true so the engine's built-in default
    // is undisturbed when neither config nor CLI specifies these.
    IRRender::SubdivisionMode subdivisionMode_ = IRRender::SubdivisionMode::FULL;
    bool subdivisionModeExplicit_ = false;
    int baseSubdivisions_ = 1;
    bool baseSubdivisionsExplicit_ = false;
};

struct CliOverrides {
    bool modeSet_ = false;
    PerfGridMode mode_ = PerfGridMode::VoxelSet;
    bool gridSizeSet_ = false;
    int gridSize_ = 64;
    bool zoomSet_ = false;
    float zoom_ = 0.5f;
    bool yawSet_ = false;
    float yaw_ = 0.0f;
    bool yawRamp_ = false;
    bool yawRampCrops_ = false;
    bool yawRampWave_ = false;
    bool waveAmplitudeSet_ = false;
    float waveAmplitude_ = 0.0f;
    bool waveModeSet_ = false;
    WaveMode waveMode_ = WaveMode::PerCell;
    bool subdivisionModeSet_ = false;
    IRRender::SubdivisionMode subdivisionMode_ = IRRender::SubdivisionMode::FULL;
    bool baseSubdivisionsSet_ = false;
    int baseSubdivisions_ = 1;
    // Accepted and recorded for manifest/cell-ID purposes; ignored until T-221.
    int workerThreads_ = 0;
    std::string configPreset_; // path from --config-preset, empty if absent
    // `--depth-probe X,Y` (#1910): per-frame composite-depth readback + log at
    // main-framebuffer texture pixel (X,Y), top-left origin (framebuffer-texture
    // space, not window/screenshot pixels). Off by default → no probe system
    // registered → flagless run byte-identical. Used to root-cause the #1884
    // per-axis Y-over-X face-stripe depth crossing this demo exercises.
    bool depthProbeSet_ = false;
    ivec2 depthProbePixel_{0, 0};
};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {0.5f, vec2(0, 0), 0.0f, "fit_grid"},
    {1.0f, vec2(0, 0), 0.0f, "zoom1_origin"},
    // Profiler-overlay regression shot — captures the perf_stats overlay
    // backing T-275 acceptance: future overlay-breaking diffs (font, layout,
    // CPU/GPU readout) trip the render-verify image compare.
    {0.5f, vec2(0, 0), 0.0f, "profiler_overlay"},
    // Rotated-zoom regression shots: at a non-cardinal residual yaw the
    // per-axis scatter path replaces the cardinal gather, and camera zoom
    // must still scale the on-screen content (not just shrink the cull
    // viewport). Voxels in zoom4_rot must render 4x larger than zoom1_rot.
    {1.0f, vec2(0, 0), 0.35f, "zoom1_rot"},
    {4.0f, vec2(0, 0), 0.35f, "zoom4_rot"},
    // Rotated-pan regression pair: the same camera offset at the same zoom,
    // cardinal vs rotated. The world content at screen center must match
    // (rotation pivots about the focus) — a rotated pan that drifts at the
    // wrong rate breaks this pairing.
    {4.0f, vec2(16, 8), 0.0f, "zoom4_pan"},
    {4.0f, vec2(16, 8), 0.35f, "zoom4_rot_pan"},
};

// Gallery-mode shots (#2411 reduction harness): the same scene swept from
// cardinal through mid-quadrant residual yaws, plus a close-up on the contact
// pairs at screen center. Ordered by increasing yaw so consecutive shots stay
// a small lighting-convergence step apart (mirrors the yaw-ramp ordering).
constexpr IRVideo::AutoScreenshotShot kGalleryShots[] = {
    {4.0f, vec2(0, 0), 0.0f, "gal_yaw000"},
    {4.0f, vec2(0, 0), 0.35f, "gal_yaw020"},
    {4.0f, vec2(0, 0), 0.7f, "gal_yaw040"},
    {4.0f, vec2(0, 0), 1.2217f, "gal_yaw070"},
    {4.0f, vec2(0, 0), IRMath::kHalfPi, "gal_yaw090"},
    {8.0f, vec2(0, 0), 0.35f, "gal_pairs_zoom8_yaw020"},
    {8.0f, vec2(0, 0), 0.7f, "gal_pairs_zoom8_yaw040"},
};

// --yaw-ramp harness: the rotated-solidity validation set (#1882 / #1883).
// Run with `--mode dense --yaw-ramp --auto-screenshot` (driven by
// scripts/dev/perf-grid-rotate-sweep). The original "uniform 36-step ramp"
// labelled its rows by `i/n*360` and could not tell which render path drew a
// pose, so a row called "cardinal" might secretly be a per-axis residual frame
// (the #1882 misdiagnosis). This set fixes that with two tiers plus a measured
// per-pose render-path label:
//
//   * Cardinal-isolation tier — the four EXACT cardinals (0/90/180/270). At
//     residual≈0 the per-axis textures release, so these capture the
//     single-canvas cardinal path; the harness MEASURES that path per pose
//     (logRampPose) and scores coverage so "is the single-canvas path clean at
//     this cardinal?" is answered directly, not assumed.
//   * Near-cardinal residual tier — dense sampling APPROACHING 90/180/270
//     (±1..10°), where the per-axis face-alignment seams / coverage bands peak
//     (#1883). These render through the per-axis path by design; the zoom pass
//     (small --grid-size + high --zoom + --yaw-ramp-crops) makes the fine seams
//     a measurable silhouette signal the whole-cube pass under-resolves.
//
// Each capture's render path (single-canvas vs per-axis) is MEASURED at the
// settled frame via the onCaptureFrame_ hook (logRampPose below) so a sweep row
// labelled "cardinal" is unambiguous. The vectors must outlive the game loop,
// so they are file-scope globals; g_yawRampLabels backs the shots' label_
// pointers (filled once, never grown, so the c_str() pointers stay valid).
std::vector<IRVideo::AutoScreenshotShot> g_yawRampShots;
std::vector<std::string> g_yawRampLabels;
IRVideo::RoiCrop g_yawRampCenterCrop{};

PerfGridSettings g_settings{};
CliOverrides g_cliOverrides{};

struct RampPose {
    float yawRadians_;
    bool cardinal_;
    std::string label_;
};

// Both pose tables below start from the same four exact cardinals.
void pushCardinalPoses(std::vector<RampPose> &poses) {
    const float cardinals[4] = {0.0f, IRMath::kHalfPi, IRMath::kPi, 3.0f * IRMath::kHalfPi};
    const char *cardinalLabels[4] = {"card000", "card090", "card180", "card270"};
    for (int k = 0; k < 4; ++k) {
        poses.push_back({cardinals[k], true, cardinalLabels[k]});
    }
}

// Default --yaw-ramp table (#1882/#1883): the four exact cardinals plus a
// dense near-cardinal residual band (±1..10°), where the per-axis
// face-alignment seams peak.
std::vector<RampPose> buildNearCardinalPoses() {
    std::vector<RampPose> poses;
    pushCardinalPoses(poses);

    const int residualBases[3] = {90, 180, 270};
    const float offsetsDeg[8] = {-10.0f, -6.0f, -3.0f, -1.0f, 1.0f, 3.0f, 6.0f, 10.0f};
    for (int base : residualBases) {
        for (float off : offsetsDeg) {
            const float deg = static_cast<float>(base) + off;
            const float rad = deg * IRMath::kPi / 180.0f;
            const int absOff = static_cast<int>(IRMath::abs(off) + 0.5f);
            std::string label = "near" + std::to_string(base) + "_" + (off < 0.0f ? "m" : "p") +
                                (absOff < 10 ? "0" : "") + std::to_string(absOff);
            poses.push_back({rad, false, std::move(label)});
        }
    }
    return poses;
}

// --yaw-ramp-wave (#2332): wider-angle pose table for the --wave-freeze
// coset-collision sweep. #2331's defect (per-cell wave positions colliding
// onto the same cardinal iso cell) isn't confined to the narrow near-cardinal
// band the default table samples — it shows up across the whole residual
// range — so this table covers quadrant-0 densely (5/10/20/30/40°) and
// spot-checks the other three quadrants at two representative offsets
// (10/30°) rather than repeating the full set four times over.
std::vector<RampPose> buildWaveSweepPoses() {
    std::vector<RampPose> poses;
    pushCardinalPoses(poses);

    const float quadrant0Deg[5] = {5.0f, 10.0f, 20.0f, 30.0f, 40.0f};
    for (float deg : quadrant0Deg) {
        const int rounded = static_cast<int>(deg + 0.5f);
        std::string label = "wave_q0_p" + (rounded < 10 ? std::string("0") : std::string("")) +
                            std::to_string(rounded);
        poses.push_back({deg * IRMath::kPi / 180.0f, false, std::move(label)});
    }

    const int quadrantBases[3] = {90, 180, 270};
    const char *quadrantNames[3] = {"q1", "q2", "q3"};
    const float matchedOffsetsDeg[2] = {10.0f, 30.0f};
    for (int qi = 0; qi < 3; ++qi) {
        for (float off : matchedOffsetsDeg) {
            const float deg = static_cast<float>(quadrantBases[qi]) + off;
            const int roundedOff = static_cast<int>(off + 0.5f);
            std::string label = "wave_" + std::string(quadrantNames[qi]) + "_p" +
                                (roundedOff < 10 ? std::string("0") : std::string("")) +
                                std::to_string(roundedOff);
            poses.push_back({deg * IRMath::kPi / 180.0f, false, std::move(label)});
        }
    }
    return poses;
}

void buildYawRampShots() {
    // Every pose uses the run's camera zoom — the sweep drives a wide
    // whole-cube coverage pass (default 0.8) and a tight small-cube zoom pass
    // (high --zoom + small --grid-size) through the same shot table.
    const float zoom = g_settings.initialZoom_;

    std::vector<RampPose> poses =
        g_cliOverrides.yawRampWave_ ? buildWaveSweepPoses() : buildNearCardinalPoses();

    // Order by yaw so consecutive shots are a small step apart, keeping the
    // iterative lighting (light volume / AO / sun-shadow) converged frame to
    // frame; a large jump captures it mid-converge and unlit faces read as
    // spurious "holes". Only the three region gaps (~70°) jump far — the
    // settle window absorbs them.
    std::sort(poses.begin(), poses.end(), [](const RampPose &a, const RampPose &b) {
        return a.yawRadians_ < b.yawRadians_;
    });

    const bool useCrops = g_cliOverrides.yawRampCrops_;
    if (useCrops) {
        ivec2 fb{0, 0};
        IRWindow::getFramebufferSize(fb);
        const int cw = IRMath::max(1, fb.x / 2);
        const int ch = IRMath::max(1, fb.y / 2);
        g_yawRampCenterCrop = {(fb.x - cw) / 2, (fb.y - ch) / 2, cw, ch, "center"};
    }

    g_yawRampLabels.clear();
    g_yawRampLabels.reserve(poses.size());
    for (const RampPose &p : poses) {
        g_yawRampLabels.push_back(p.label_);
    }

    g_yawRampShots.clear();
    g_yawRampShots.reserve(poses.size());
    for (std::size_t i = 0; i < poses.size(); ++i) {
        IRVideo::AutoScreenshotShot shot{};
        shot.zoom_ = zoom;
        shot.cameraIso_ = vec2(0, 0);
        shot.yawRadians_ = poses[i].yawRadians_;
        shot.label_ = g_yawRampLabels[i].c_str();
        // Crops only on the residual-band poses (where the seams live) and only
        // when the zoom pass asked for them.
        if (useCrops && !poses[i].cardinal_) {
            shot.crops_ = &g_yawRampCenterCrop;
            shot.numCrops_ = 1;
        }
        g_yawRampShots.push_back(shot);
    }
}

// onCaptureFrame_ hook: measure which render path actually drew this pose so
// the sweep can label every row unambiguously. The main canvas's per-axis
// textures are allocated only while a residual rotation is being smoothed, so
// isAllocated() at the settled capture frame is the ground truth (a 'cardinal'
// row that still reports peraxis is the #1882 failure). Reads live ECS the
// harness cannot; emits one greppable line the sweep scorer joins by index.
void logRampPose(int shotIndex) {
    bool perAxisActive = false;
    const IREntity::EntityId mainCanvas = IRRender::getCanvas("main");
    if (mainCanvas != IREntity::kNullEntity) {
        auto perAxis =
            IREntity::getComponentOptional<IRComponents::C_PerAxisTrixelCanvases>(mainCanvas);
        if (perAxis.has_value()) {
            perAxisActive = perAxis.value()->isAllocated();
        }
    }
    const float residualDeg = IRPrefab::Camera::getResidualYaw() * 180.0f / IRMath::kPi;
    const float nominalDeg = (shotIndex >= 0 && shotIndex < static_cast<int>(g_yawRampShots.size()))
                                 ? g_yawRampShots[shotIndex].yawRadians_ * 180.0f / IRMath::kPi
                                 : 0.0f;
    const char *label = (shotIndex >= 0 && shotIndex < static_cast<int>(g_yawRampLabels.size()))
                            ? g_yawRampLabels[shotIndex].c_str()
                            : "?";
    IR_LOG_INFO(
        "RAMP-POSE idx={} label={} yaw_deg={:.3f} path={} residual_deg={:.4f}",
        shotIndex,
        label,
        nominalDeg,
        perAxisActive ? "peraxis" : "single",
        residualDeg
    );
}
int g_autoProfileFrames = 0;
int g_autoProfileCount = 0;
int g_autoWarmupFrames = 0;
// --occlusion-cull (#1294 child 3/3): force the voxel-pool chunk-occlusion HZB
// pre-pass ON (off by default in the engine). This is the measurement + verify
// toggle for the cull: pair `--mode voxel_set --auto-profile` runs with and
// without it to read the realized `voxelStage1` reduction (the 0.97 ceiling on
// the many-small-entity grid), and pair `--auto-screenshot` runs to prove the
// output is bit-identical cull-on vs cull-off (fully-occluded voxels write
// nothing, so any diff would be a cull bug). Intentional drift: on a
// discontinuous camera move the cull self-disables for one frame (stale Hi-Z),
// which can produce a one-frame silhouette pop — by design, not a regression.
bool g_occlusionCull = false;
// --no-overlay (#1294 child 3/3): drop PERF_STATS_OVERLAY from the render
// pipeline. The overlay bakes live CPU/GPU timing text into every frame, which
// is run-variant and defeats a bit-identical screenshot compare. Off (overlay
// present) by default; pass it to capture a deterministic frame, e.g. to prove
// cull-on renders pixel-identical to cull-off on the heavily-occluded voxel_set
// scene. The --auto-profile path enables frame timing explicitly, so timing
// still works under --no-overlay.
bool g_noOverlay = false;
// --no-sun-shadows (#1812): disable sun shadows globally so the render cull
// collapses from the shadow-feeder-widened extent back to the visible viewport
// (`IRMath::shadowFeederIsoBounds` only widens when `getSunShadowsEnabled()`;
// render/CLAUDE.md "Lighting culling invariants"). Pairs with --occlusion-cull
// --auto-profile to measure the per-voxel cull's capture in the regime where the
// WHOLE frustum is legally cullable — with shadows on, visibleVoxelCount is
// dominated by off-screen shadow feeders the cull must not drop, so the ratio
// understates the mechanism. This is the baseline number the widened-domain
// feeder-occlusion follow-on needs.
bool g_noSunShadows = false;
IRRender::DebugOverlayMode g_debugOverlay = IRRender::DebugOverlayMode::NONE;
// --no-per-voxel-occlusion (#1812): with --occlusion-cull on, disable ONLY the
// per-voxel Hi-Z refine (keep the #1294 chunk pre-pass). This is the marginal
// acceptance-gate isolation: --occlusion-cull A/B measures the UNION of the two
// culls, and the chunk cull owns the pre-existing `max_delta 96` silhouette
// holes (its own bug, filed separately), so union-vs-no-cull can never be
// bit-identical. The per-voxel test's real, PR-scoped contribution is the
// MARGINAL delta — cull-with-per-voxel vs cull-without — which is bit-identical
// (it drops zero visible voxels; the 2×2 isolation E2==A proved it). No-op
// without --occlusion-cull.
bool g_noPerVoxelOcclusion = false;
// --wave-freeze (#2332): bake each per-cell wave's phase-0 offset into the
// cell's spawn position instead of attaching C_PeriodicIdle. The wave-scene
// geometry (WaveMode::PerCell's per-cell (x+y+z) phase gradient) is the only
// content shape that produces (1,1,1)-coset voxel pairs the #2331 defect
// needs; a live C_PeriodicIdle wave is not byte-identical run-to-run, so a
// render-verify regression tier needs the frozen static twin instead. Off by
// default -> flagless spawn path is untouched (byte-identical to master).
bool g_waveFreeze = false;

PerfGridMode parseMode(const std::string &value) {
    if (value == "voxel_set" || value == "voxel") {
        return PerfGridMode::VoxelSet;
    }
    if (value == "sdf" || value == "shape") {
        return PerfGridMode::Sdf;
    }
    if (value == "dense_set" || value == "dense") {
        return PerfGridMode::DenseSet;
    }
    if (value == "hollow_set" || value == "hollow") {
        return PerfGridMode::HollowSet;
    }
    if (value == "gallery") {
        return PerfGridMode::Gallery;
    }
    IR_LOG_WARN("Unknown perf_grid mode '{}'; using voxel_set", value);
    return PerfGridMode::VoxelSet;
}

const char *modeName(PerfGridMode mode) {
    switch (mode) {
    case PerfGridMode::VoxelSet:
        return "voxel_set";
    case PerfGridMode::Sdf:
        return "sdf";
    case PerfGridMode::DenseSet:
        return "dense_set";
    case PerfGridMode::HollowSet:
        return "hollow_set";
    case PerfGridMode::Gallery:
        return "gallery";
    }
    return "voxel_set";
}

WaveMode parseWaveMode(const std::string &value) {
    if (value == "rigid") {
        return WaveMode::Rigid;
    }
    if (value == "per_cell") {
        return WaveMode::PerCell;
    }
    IR_LOG_WARN("Unknown perf_grid wave_mode '{}'; using per_cell", value);
    return WaveMode::PerCell;
}

const char *waveModeName(WaveMode mode) {
    switch (mode) {
    case WaveMode::Rigid:
        return "rigid";
    case WaveMode::PerCell:
        return "per_cell";
    }
    return "rigid";
}

template <typename T> void readLuaValue(sol::table table, const char *key, T &out) {
    sol::object value = table[key];
    if (value.valid() && value.is<T>()) {
        out = value.as<T>();
    }
}

void applyPerfGridTable(sol::table perfGrid) {
    std::string mode = modeName(g_settings.mode_);
    readLuaValue(perfGrid, "mode", mode);
    g_settings.mode_ = parseMode(mode);
    readLuaValue(perfGrid, "grid_size", g_settings.gridSize_);
    readLuaValue(perfGrid, "spacing", g_settings.spacing_);
    std::string waveMode = waveModeName(g_settings.waveMode_);
    readLuaValue(perfGrid, "wave_mode", waveMode);
    g_settings.waveMode_ = parseWaveMode(waveMode);
    readLuaValue(perfGrid, "wave_amplitude", g_settings.waveAmplitude_);
    readLuaValue(perfGrid, "wave_period_seconds", g_settings.wavePeriodSeconds_);
    readLuaValue(perfGrid, "wave_offscreen", g_settings.waveOffscreen_);
    // zoom, subdivision_mode, and base_subdivisions are also settable from
    // config.lua and preset files (not just CLI flags).
    readLuaValue(perfGrid, "zoom", g_settings.initialZoom_);

    std::string subModeStr;
    readLuaValue(perfGrid, "subdivision_mode", subModeStr);
    if (subModeStr == "none") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::NONE;
        g_settings.subdivisionModeExplicit_ = true;
    } else if (subModeStr == "position_only") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::POSITION_ONLY;
        g_settings.subdivisionModeExplicit_ = true;
    } else if (subModeStr == "full") {
        g_settings.subdivisionMode_ = IRRender::SubdivisionMode::FULL;
        g_settings.subdivisionModeExplicit_ = true;
    }

    int baseSub = 0;
    readLuaValue(perfGrid, "base_subdivisions", baseSub);
    // 0 treated as absent; valid subdivision counts are >= 1.
    if (baseSub > 0) {
        g_settings.baseSubdivisions_ = baseSub;
        g_settings.baseSubdivisionsExplicit_ = true;
    }
}

void applyConfigTable() {
    IRScript::LuaScript configScript{IREngine::resolveScriptPath("config.lua").c_str()};
    sol::table perfGrid = configScript.getTable("perf_grid");
    if (!perfGrid.valid()) {
        return;
    }
    applyPerfGridTable(perfGrid);
}

void applyConfigPreset(const std::string &presetPath) {
    if (presetPath.empty()) {
        return;
    }
    IRScript::LuaScript presetScript{presetPath.c_str()};
    sol::table perfGrid = presetScript.getTable("perf_grid");
    if (!perfGrid.valid()) {
        IR_LOG_WARN("--config-preset '{}': no perf_grid table found, skipping", presetPath);
        return;
    }
    IR_LOG_INFO("Applying config preset: {}", presetPath);
    applyPerfGridTable(perfGrid);
}

void applyCliOverrides() {
    if (g_cliOverrides.modeSet_) {
        g_settings.mode_ = g_cliOverrides.mode_;
    }
    if (g_cliOverrides.gridSizeSet_) {
        g_settings.gridSize_ = g_cliOverrides.gridSize_;
    }
    if (g_cliOverrides.zoomSet_) {
        g_settings.initialZoom_ = g_cliOverrides.zoom_;
    }
    if (g_cliOverrides.yawSet_) {
        g_settings.initialYaw_ = g_cliOverrides.yaw_;
    }
    if (g_cliOverrides.waveAmplitudeSet_) {
        g_settings.waveAmplitude_ = g_cliOverrides.waveAmplitude_;
    }
    if (g_cliOverrides.waveModeSet_) {
        g_settings.waveMode_ = g_cliOverrides.waveMode_;
    }
    // CLI subdivision flags supersede config/preset values.
    if (g_cliOverrides.subdivisionModeSet_) {
        g_settings.subdivisionMode_ = g_cliOverrides.subdivisionMode_;
        g_settings.subdivisionModeExplicit_ = true;
    }
    if (g_cliOverrides.baseSubdivisionsSet_) {
        g_settings.baseSubdivisions_ = g_cliOverrides.baseSubdivisions_;
        g_settings.baseSubdivisionsExplicit_ = true;
    }
}

// Register perf_grid's custom flags on the engine-owned parser. --help /
// --auto-screenshot / --config-preset are pre-registered by the Parser ctor;
// IREngine::init(argc, argv) parses common + these in one pass, so --help lists
// every flag and exits before any window/GL/Metal init (epic #2057 P3, #2060).
void registerCliArgs() {
    IRArgs::Parser &args = IREngine::args();
    args.optionalInt(
        "--auto-profile",
        "Run N frames (default 300) collecting CPU+GPU per-stage timing, then exit",
        300
    );
    args.flag(
        "--occlusion-cull",
        "Force the voxel-pool chunk-occlusion HZB pre-pass ON (off by default)"
    );
    args.flag("--no-overlay", "Drop PERF_STATS_OVERLAY so a captured frame is deterministic");
    args.flag(
        "--no-sun-shadows",
        "Disable sun shadows so the cull collapses to the visible viewport (occlusion-cull "
        "measurement baseline)"
    );
    args.flag(
        "--no-per-voxel-occlusion",
        "With --occlusion-cull, disable only the #1812 per-voxel Hi-Z refine (keep the #1294 chunk "
        "cull) — the marginal-gate isolation"
    );
    args.flag(
        "--wave-freeze",
        "Bake each cell's wave phase-0 offset into its spawn position instead of "
        "attaching C_PeriodicIdle; makes voxel_set/sdf wave content static and "
        "deterministic (#2332)"
    );
    args.string(
        "--mode",
        "Scene mode: voxel_set | sdf | dense_set | hollow_set | gallery",
        "voxel_set"
    );
    args.integer("--grid-size", "Grid edge in cells", 64);
    args.number("--zoom", "Initial camera zoom", 0.5f);
    args.number("--yaw", "Initial camera Z-yaw in radians", 0.0f);
    args.flag("--yaw-ramp", "Rotated-solidity validation sweep (#1882/#1883)");
    args.flag(
        "--yaw-ramp-crops",
        "Attach a center ROI crop to each near-cardinal residual --yaw-ramp pose"
    );
    args.flag(
        "--yaw-ramp-wave",
        "Use the wider-angle wave-freeze coset-collision pose table instead of the "
        "near-cardinal residual table (#2332)"
    );
    args.number("--wave-amplitude", "Per-frame idle wave amplitude (0 = static scene)", 0.0f);
    args.string(
        "--wave-mode",
        "Wave motion: per_cell (traveling wave) | rigid (block glides right)",
        "per_cell"
    );
    args.string("--subdivision-mode", "Trixel subdivision: none | position_only | full", "full");
    args.integer("--base-subdivisions", "Base trixel subdivision count", 1);
    args.integer("--worker-threads", "Recorded for manifest/cell-ID; thread wiring is T-221", 0);
    args.string("--depth-probe", "Per-frame composite-depth readback at framebuffer pixel X,Y", "");
    args.enumValue(
        "--debug-overlay",
        "Lighting/composite debug overlay",
        {"none",
         "ao",
         "light_level",
         "shadow",
         "peraxis_id",
         "peraxis_origin",
         "unlit",
         "peraxis_margin"},
        "none"
    );
}

// Read the parsed values back into the settings/override structs. Runs AFTER
// IREngine::init(argc, argv) has parsed. The CLI-override precedence (defaults <
// config.lua < preset < CLI) is unchanged: a flag only sets its `*Set_` gate
// when actually provided, so config/preset values stand otherwise.
void readCliArgs() {
    const IRArgs::Parser &args = IREngine::args();

    // Engine-common args now own these (P1 #2058 retired the local reads).
    g_autoWarmupFrames = args.autoScreenshotWarmupFrames();
    g_cliOverrides.configPreset_ = args.configPreset();

    // --auto-profile: 0 when absent, else the frame count (300 if bare).
    if (args.wasProvided("--auto-profile")) {
        g_autoProfileFrames = args.getInt("--auto-profile");
    }
    g_occlusionCull = args.getFlag("--occlusion-cull");
    g_noOverlay = args.getFlag("--no-overlay");
    g_noSunShadows = args.getFlag("--no-sun-shadows");
    g_debugOverlay = IRRender::debugOverlayModeFromString(args.getEnum("--debug-overlay").c_str());
    g_noPerVoxelOcclusion = args.getFlag("--no-per-voxel-occlusion");
    g_waveFreeze = args.getFlag("--wave-freeze");

    if (args.wasProvided("--mode")) {
        g_cliOverrides.mode_ = parseMode(args.getString("--mode"));
        g_cliOverrides.modeSet_ = true;
    }
    if (args.wasProvided("--grid-size")) {
        const int gridSize = args.getInt("--grid-size");
        if (gridSize > 0) {
            g_cliOverrides.gridSize_ = gridSize;
            g_cliOverrides.gridSizeSet_ = true;
        }
    }
    if (args.wasProvided("--zoom")) {
        const float zoom = args.getFloat("--zoom");
        if (zoom > 0.0f) {
            g_cliOverrides.zoom_ = zoom;
            g_cliOverrides.zoomSet_ = true;
        }
    }
    if (args.wasProvided("--yaw")) {
        g_cliOverrides.yaw_ = args.getFloat("--yaw");
        g_cliOverrides.yawSet_ = true;
    }
    g_cliOverrides.yawRamp_ = args.getFlag("--yaw-ramp");
    g_cliOverrides.yawRampCrops_ = args.getFlag("--yaw-ramp-crops");
    g_cliOverrides.yawRampWave_ = args.getFlag("--yaw-ramp-wave");
    if (args.wasProvided("--wave-amplitude")) {
        // 0.0 = static scene (no per-frame voxel motion). Useful for isolating
        // per-frame upload cost in profiler runs.
        g_cliOverrides.waveAmplitude_ = args.getFloat("--wave-amplitude");
        g_cliOverrides.waveAmplitudeSet_ = true;
    }
    if (args.wasProvided("--wave-mode")) {
        g_cliOverrides.waveMode_ = parseWaveMode(args.getString("--wave-mode"));
        g_cliOverrides.waveModeSet_ = true;
    }
    if (args.wasProvided("--subdivision-mode")) {
        const std::string mode = args.getString("--subdivision-mode");
        if (mode == "none") {
            g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::NONE;
            g_cliOverrides.subdivisionModeSet_ = true;
        } else if (mode == "position_only") {
            g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::POSITION_ONLY;
            g_cliOverrides.subdivisionModeSet_ = true;
        } else if (mode == "full") {
            g_cliOverrides.subdivisionMode_ = IRRender::SubdivisionMode::FULL;
            g_cliOverrides.subdivisionModeSet_ = true;
        } else {
            IR_LOG_WARN("Unknown --subdivision-mode '{}'; expected none|position_only|full", mode);
        }
    }
    if (args.wasProvided("--base-subdivisions")) {
        const int sub = args.getInt("--base-subdivisions");
        if (sub > 0) {
            g_cliOverrides.baseSubdivisions_ = sub;
            g_cliOverrides.baseSubdivisionsSet_ = true;
        }
    }
    if (args.wasProvided("--worker-threads")) {
        // Accepted for cell-ID purposes by perf_grid_matrix.sh; ignored until
        // T-221 wires enkiTS thread-pool sizing.
        const int wt = args.getInt("--worker-threads");
        if (wt >= 0) {
            g_cliOverrides.workerThreads_ = wt;
        }
    }
    if (args.wasProvided("--depth-probe")) {
        // `--depth-probe X,Y` (#1910): IRArgs has no pair type, so the comma
        // split lives demo-side (shared IRPrefab::DepthProbe::parsePixelArg).
        ivec2 pixel;
        if (IRPrefab::DepthProbe::parsePixelArg(
                args.getString("--depth-probe"),
                pixel,
                "--depth-probe"
            )) {
            g_cliOverrides.depthProbeSet_ = true;
            g_cliOverrides.depthProbePixel_ = pixel;
        }
    }
}

void validateSettings() {
    g_settings.gridSize_ = IRMath::max(1, g_settings.gridSize_);
    g_settings.spacing_ = IRMath::max(0.25f, g_settings.spacing_);
    g_settings.wavePeriodSeconds_ = IRMath::max(0.1f, g_settings.wavePeriodSeconds_);

    const int poolEdge = IRRender::VoxelPoolConfig::getEdge();
    if (g_settings.mode_ == PerfGridMode::VoxelSet && g_settings.gridSize_ > poolEdge) {
        IR_LOG_WARN(
            "voxel_set mode requested grid_size={}, but the global voxel pool holds only {}^3 "
            "single-voxel entities. Clamping grid_size to {}.",
            g_settings.gridSize_,
            poolEdge,
            poolEdge
        );
        g_settings.gridSize_ = poolEdge;
    }

    const int entityCount = g_settings.gridSize_ * g_settings.gridSize_ * g_settings.gridSize_;
    if (g_settings.mode_ == PerfGridMode::VoxelSet &&
        entityCount == IRRender::VoxelPoolConfig::getTotalSize()) {
        IR_LOG_WARN(
            "perf_grid voxel_set mode will allocate all {} voxels in the global pool; "
            "do not add particle or other voxel-pool consumers to this scene.",
            IRRender::VoxelPoolConfig::getTotalSize()
        );
    }
}

Color colorForCell(int x, int y, int z, int gridSize) {
    const float denom = static_cast<float>(IRMath::max(gridSize - 1, 1));
    return Color{
        static_cast<std::uint8_t>(80 + 120.0f * (static_cast<float>(x) / denom)),
        static_cast<std::uint8_t>(120 + 100.0f * (static_cast<float>(y) / denom)),
        static_cast<std::uint8_t>(160 + 80.0f * (static_cast<float>(z) / denom)),
        255
    };
}

C_PeriodicIdle makeWaveIdle(int x, int y, int z) {
    const float amplitude =
        g_settings.waveOffscreen_ ? g_settings.waveAmplitude_ * 6.0f : g_settings.waveAmplitude_;

    // Rigid: whole block glides along the screen-right iso axis, every
    // cell in phase. World (1,-1,0) -> iso.x=-2, iso.y=0 (screen-Y flat
    // at cardinal yaw); pos3DtoPos2DScreen negates iso.x via its
    // vec2(-1, screenY) factor, so iso.x=-2 lands at +screen-x -- right,
    // not left. PerCell: the default; a per-cell phase gradient makes a
    // z-axis wave travel through the volume.
    vec3 amplitudeVec;
    float phase = 0.0f;
    if (g_settings.waveMode_ == WaveMode::Rigid) {
        amplitudeVec = IRMath::normalize(vec3(1.0f, -1.0f, 0.0f)) * amplitude;
    } else {
        const float wavelength = IRMath::max(8.0f, static_cast<float>(g_settings.gridSize_) * 0.5f);
        phase = IRMath::kTwoPi * static_cast<float>(x + y + z) / wavelength;
        amplitudeVec = vec3(0.0f, 0.0f, amplitude);
    }

    C_PeriodicIdle idle{amplitudeVec, g_settings.wavePeriodSeconds_, phase};
    idle.addStageDurationSeconds(
        0.0f,
        g_settings.wavePeriodSeconds_ * 0.5f,
        -1.0f,
        1.0f,
        IREasingFunctions::kSineEaseInOut
    );
    idle.addStageDurationSeconds(
        g_settings.wavePeriodSeconds_ * 0.5f,
        g_settings.wavePeriodSeconds_ * 0.5f,
        1.0f,
        -1.0f,
        IREasingFunctions::kSineEaseInOut
    );
    return idle;
}

// #2332 --wave-freeze: what makeWaveIdle's traveling wave would read at
// phase 0 (t=0), i.e. before PERIODIC_IDLE's first tick(). Delegates to
// C_PeriodicIdle::valueAtAngle so the wrap + stage-search + easing lives in
// one place next to tick(), rather than re-deriving the sine-ease math here.
vec3 waveFreezeOffset(int x, int y, int z) {
    C_PeriodicIdle idle = makeWaveIdle(x, y, z);
    return idle.valueAtAngle(idle.angle_);
}

vec3 positionForCell(int x, int y, int z) {
    const float center = (static_cast<float>(g_settings.gridSize_) - 1.0f) * 0.5f;
    return (vec3(x, y, z) - vec3(center)) * g_settings.spacing_;
}

// Gallery mode (#2411 reduction harness). Static, no wave, no idle. Every
// entity is far enough from its neighbors that silhouettes never overlap on
// screen at the gallery shot zooms, so a broken cube is readable in isolation.
void createGalleryEntities() {
    struct GalleryShape {
        const char *label_;
        ivec3 size_;
        bool carveL_; // deactivate the (+x,+y) quadrant → L cross-section
    };
    constexpr GalleryShape kShapes[] = {
        {"cube1", ivec3(1, 1, 1), false},
        {"cube2", ivec3(2, 2, 2), false},
        {"cube3", ivec3(3, 3, 3), false},
        {"cube4", ivec3(4, 4, 4), false},
        {"slab", ivec3(6, 3, 2), false},
        {"tower", ivec3(1, 1, 5), false},
        {"plate", ivec3(5, 5, 1), false},
        {"lshape", ivec3(4, 4, 4), true},
    };
    constexpr Color kPalette[] = {
        Color{235, 120, 100, 255},
        Color{90, 200, 190, 255},
        Color{235, 200, 90, 255},
        Color{170, 120, 235, 255},
        Color{120, 220, 120, 255},
        Color{110, 150, 240, 255},
        Color{240, 150, 190, 255},
        Color{150, 230, 200, 255},
    };
    constexpr int kShapeCount = static_cast<int>(sizeof(kShapes) / sizeof(kShapes[0]));
    constexpr int kStride = 9; // ≥ max shape edge (6) + clear air

    auto spawnShape = [&](const GalleryShape &shape, vec3 pos, Color color) {
        EntityId entity = IREntity::createEntity(
            C_LocalTransform{pos},
            C_VoxelSetNew{shape.size_, color, false},
            C_Modifiers{}
        );
        if (shape.carveL_) {
            auto setOpt = IREntity::getComponentOptional<C_VoxelSetNew>(entity);
            if (setOpt.has_value()) {
                setOpt.value()->carve([](vec3 localPos) {
                    return localPos.x >= 2.0f && localPos.y >= 2.0f;
                });
            }
        }
        return entity;
    };

    // Row A — isolated shapes at INTEGER world positions, spread along the
    // screen-right iso axis (world (1,-1,0)).
    // Row B — the same shapes at FRACTIONAL offsets (the mid-wave regime a
    // gliding entity passes through), isolated the same way.
    for (int i = 0; i < kShapeCount; ++i) {
        // Integer spread: row A must sit at EXACT integer world positions so
        // the two rows isolate the position-fraction variable cleanly.
        const float d = static_cast<float>((i - kShapeCount / 2) * kStride);
        const Color color = kPalette[i];
        spawnShape(kShapes[i], vec3(d - 9.0f, -d - 9.0f, 0.0f), color);
        spawnShape(kShapes[i], vec3(d + 9.0f + 0.37f, -d + 9.0f + 0.13f, 0.5f), color);
    }

    // Contact pairs at screen center — the minimal cross-entity cases the
    // rows deliberately exclude. No occupancy stamping: the shared faces
    // stay marked exposed, exactly like the moving per-cell grid.
    const Color pairColorA{230, 230, 230, 255};
    const Color pairColorB{140, 140, 220, 255};
    // Face-to-face contact along x.
    spawnShape({"pairA0", ivec3(2, 2, 2), false}, vec3(-5.0f, 0.0f, 0.0f), pairColorA);
    spawnShape({"pairA1", ivec3(2, 2, 2), false}, vec3(-3.0f, 0.0f, 0.0f), pairColorB);
    // Staircase contact: half-cell vertical offset across the shared face.
    spawnShape({"pairB0", ivec3(2, 2, 2), false}, vec3(3.0f, -3.0f, 0.0f), pairColorA);
    spawnShape({"pairB1", ivec3(2, 2, 2), false}, vec3(5.0f, -3.0f, -0.5f), pairColorB);
}

void createGridEntities() {
    if (g_settings.mode_ == PerfGridMode::Gallery) {
        IR_LOG_INFO("Creating perf_grid gallery scene (rotated-solidity validation, #2411)");
        createGalleryEntities();
        return;
    }
    const int n = g_settings.gridSize_;
    const int expectedEntities = n * n * n;
    IR_LOG_INFO(
        "Creating perf_grid mode={} grid_size={} entity_count={} spacing={} wave_mode={} "
        "wave_amplitude={} wave_period={}",
        modeName(g_settings.mode_),
        n,
        expectedEntities,
        g_settings.spacing_,
        waveModeName(g_settings.waveMode_),
        g_settings.waveAmplitude_,
        g_settings.wavePeriodSeconds_
    );
    if (g_settings.mode_ == PerfGridMode::Sdf) {
        IR_LOG_INFO(
            "perf_grid sdf: geometry is SDF-only — voxel pool stays empty, so occupancy-driven AO "
            "and light-volume LOS do not see these boxes. Visuals differ from voxel_set; see "
            "docs/perf/metal_perf_grid_baseline.md (IRPerfGrid mode parity)."
        );
    }

    if (g_settings.mode_ == PerfGridMode::DenseSet || g_settings.mode_ == PerfGridMode::HollowSet) {
        // Single big `C_VoxelSetNew` of size n³ centered at the origin — same
        // pool occupancy as voxel_set mode, but every voxel shares the entity
        // so the only thing varying is the active-mask bit pattern. Dense
        // leaves every slot active; hollow drops the interior so the mask
        // covers only the cube's outer shell (~6n² / n³ → ≤10% for n ≥ 60),
        // exercising the T-287 compaction path on the same voxel buffer.
        const Color color = colorForCell(n / 2, n / 2, n / 2, n);
        const ivec3 size{n, n, n};
        const vec3 originOffset{-(n - 1) * 0.5f * g_settings.spacing_};
        EntityId rootEntity = IREntity::createEntity(
            C_LocalTransform{originOffset},
            C_VoxelSetNew{size, color, true},
            C_Modifiers{}
        );
        if (g_settings.mode_ == PerfGridMode::HollowSet) {
            auto setOpt = IREntity::getComponentOptional<C_VoxelSetNew>(rootEntity);
            if (setOpt.has_value()) {
                C_VoxelSetNew &voxelSet = *setOpt.value();
                voxelSet.deactivateAll();
                // Reactivate only the cube's six bounding faces.
                voxelSet.fillPlane(0, 0, color);
                voxelSet.fillPlane(0, n - 1, color);
                voxelSet.fillPlane(1, 0, color);
                voxelSet.fillPlane(1, n - 1, color);
                voxelSet.fillPlane(2, 0, color);
                voxelSet.fillPlane(2, n - 1, color);
            }
        }
        return;
    }

    for (int z = 0; z < n; ++z) {
        for (int y = 0; y < n; ++y) {
            for (int x = 0; x < n; ++x) {
                vec3 pos = positionForCell(x, y, z);
                const Color color = colorForCell(x, y, z, n);
                // --wave-freeze (#2332): bake the phase-0 offset into the spawn
                // position and skip attaching C_PeriodicIdle, so the scene is
                // fully static. Absent the flag, this branch is untouched and
                // the idle-driven path below is byte-identical to master.
                if (g_waveFreeze) {
                    pos += waveFreezeOffset(x, y, z);
                }

                if (g_settings.mode_ == PerfGridMode::VoxelSet) {
                    EntityId cellEntity;
                    if (g_waveFreeze) {
                        cellEntity = IREntity::createEntity(
                            C_LocalTransform{pos},
                            C_VoxelSetNew{ivec3(1, 1, 1), color, false},
                            C_Modifiers{}
                        );
                    } else {
                        cellEntity = IREntity::createEntity(
                            C_LocalTransform{pos},
                            C_VoxelSetNew{ivec3(1, 1, 1), color, false},
                            makeWaveIdle(x, y, z),
                            C_Modifiers{}
                        );
                    }
                    // Cross-set face occupancy: single-voxel sets can't see
                    // their grid neighbors, so without this every interior
                    // voxel reports all six faces exposed and the compact
                    // pass rasters the full n³ volume instead of the ~6n²
                    // shell. Worse than the perf cost: under residual yaw the
                    // per-axis scatter composites those covered-face quads
                    // against the true surface at exact-tie depths (a
                    // half-integer-centered grid's same-voxel face planes
                    // share their lattice origin), and the tie resolution
                    // alternates per cell — the chevron shading stipple. The
                    // lattice is known analytically here, so stamp the
                    // occluded bits directly. Valid only while the cells
                    // actually touch (spacing 1.0) and stay touching: the
                    // rigid wave moves the whole block in phase, and a zero
                    // amplitude is static in either mode. An ACTIVE (or
                    // frozen) per-cell wave shears neighbors apart, where a
                    // stale mask would carve holes — those configurations
                    // stay unstamped.
                    const bool alwaysTouching = g_settings.waveMode_ == WaveMode::Rigid ||
                                                g_settings.waveAmplitude_ == 0.0f;
                    if (alwaysTouching && IRMath::abs(g_settings.spacing_ - 1.0f) < 1e-4f) {
                        std::uint8_t occluded = 0;
                        if (x > 0)
                            occluded |= VoxelFlags::kFaceOccludedNegX;
                        if (x + 1 < n)
                            occluded |= VoxelFlags::kFaceOccludedPosX;
                        if (y > 0)
                            occluded |= VoxelFlags::kFaceOccludedNegY;
                        if (y + 1 < n)
                            occluded |= VoxelFlags::kFaceOccludedPosY;
                        if (z > 0)
                            occluded |= VoxelFlags::kFaceOccludedNegZ;
                        if (z + 1 < n)
                            occluded |= VoxelFlags::kFaceOccludedPosZ;
                        auto cellSet = IREntity::getComponentOptional<C_VoxelSetNew>(cellEntity);
                        if (cellSet.has_value()) {
                            cellSet.value()->voxels_[0].flags_ |= occluded;
                        }
                    }
                } else {
                    if (g_waveFreeze) {
                        IREntity::createEntity(
                            C_LocalTransform{pos},
                            C_ShapeDescriptor{
                                IRRender::ShapeType::BOX,
                                vec4(1.0f, 1.0f, 1.0f, 0.0f),
                                color
                            },
                            C_Modifiers{}
                        );
                    } else {
                        IREntity::createEntity(
                            C_LocalTransform{pos},
                            C_ShapeDescriptor{
                                IRRender::ShapeType::BOX,
                                vec4(1.0f, 1.0f, 1.0f, 0.0f),
                                color
                            },
                            makeWaveIdle(x, y, z),
                            C_Modifiers{}
                        );
                    }
                }
            }
        }
    }
}

void configureLightingAndCanvas() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;

    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    // Keep C_CanvasSunShadow attached unconditionally — Metal's LIGHTING_TO_TRIXEL
    // asserts the main canvas carries it (slot 4 must be bound). --no-sun-shadows
    // instead flips the global gate below: the bake self-disables
    // (system_bake_sun_shadow_map reads getSunShadowsEnabled()) and the render
    // cull collapses from the shadow-feeder-widened extent to the visible viewport
    // (shadowFeederIsoBounds only widens when enabled), isolating the occlusion
    // cull's capture in the fully-cullable regime (see g_noSunShadows).
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    if (g_noSunShadows) {
        IRRender::setSunShadowsEnabled(false);
    }
    if (g_debugOverlay != IRRender::DebugOverlayMode::NONE) {
        IRRender::setDebugOverlay(g_debugOverlay);
    }
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IRPrefab::Fog::attachToCanvas(mainCanvas);

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, -64.0f)},
        C_LightSource{
            LightType::EMISSIVE,
            Color{90, 200, 255, 255},
            2.0f,
            // Was 180; silently capped at kLightVolumePropagateIterations
            // (32) before the propagate adapted to per-frame max radius.
            // 24 is the smallest value that still reads as the same
            // decorative glow at the demo's default camera zoom, and
            // saves ~25% on per-frame propagate dispatches on Linux/GL
            // where this stage dominates IRPerfGrid's frame budget.
            static_cast<uint8_t>(24)
        }
    );
    IRPrefab::Fog::revealRadius(0, 0, 128);
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    // Register custom flags, then let init parse common + custom in one pass
    // (--help exits here, pre-window). Read the parsed values back afterwards.
    registerCliArgs();
    IREngine::init(argc, argv);
    readCliArgs();

    IR_LOG_INFO("Starting creation: perf_grid");
    applyConfigTable();
    applyConfigPreset(g_cliOverrides.configPreset_);
    applyCliOverrides();

    // entity_count_override in config.lua overrides grid_size when nonzero
    // and the caller hasn't already set --grid-size explicitly.
    if (!g_cliOverrides.gridSizeSet_) {
        const int eco = IREngine::entityCountOverride();
        if (eco > 0) {
            const int cbrtCount = static_cast<int>(IRMath::cbrt(static_cast<double>(eco)) + 0.5);
            if (cbrtCount > 0) {
                g_settings.gridSize_ = cbrtCount;
                IR_LOG_INFO("entity_count_override={} → grid_size={}", eco, cbrtCount);
            }
        }
    }

    validateSettings();

    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }

    if (g_settings.subdivisionModeExplicit_) {
        IRRender::setSubdivisionMode(g_settings.subdivisionMode_);
    }
    if (g_settings.baseSubdivisionsExplicit_) {
        IRRender::setVoxelRenderSubdivisions(g_settings.baseSubdivisions_);
    }

    initSystems();
    initCommands();
    initEntities();

    if (g_occlusionCull) {
        IRRender::setVoxelOcclusionCullEnabled(true);
        IR_LOG_INFO(
            "Voxel chunk-occlusion cull forced ON (--occlusion-cull, #1294 child 3/3). "
            "Output stays bit-identical to cull-off; one-frame silhouette pop on a "
            "discontinuous camera move is intentional drift (stale Hi-Z self-disable)."
        );
        if (g_noPerVoxelOcclusion) {
            IRRender::setVoxelPerVoxelOcclusionEnabled(false);
            IR_LOG_INFO(
                "Per-voxel Hi-Z refine DISABLED (--no-per-voxel-occlusion, #1812). Chunk "
                "cull only; this is the marginal-gate B side (A = per-voxel on). The two "
                "must render bit-identical — the per-voxel test drops zero visible voxels."
            );
        }
    } else if (g_noPerVoxelOcclusion) {
        IR_LOG_WARN(
            "--no-per-voxel-occlusion is a no-op without --occlusion-cull (the whole "
            "cull is off, so the per-voxel refine never runs)."
        );
    }

    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
    IRRender::setCameraZoom(g_settings.initialZoom_);
    IRRender::setCameraVisualYaw(g_settings.initialYaw_);
    IR_LOG_INFO(
        "Initial camera zoom: requested={}, actual={}",
        g_settings.initialZoom_,
        IRRender::getCameraZoom().x
    );

    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    // Every UPDATE system runs in its own singleton group. PERIODIC_IDLE and
    // MODIFIER_DECAY each carry Concurrency::PARALLEL_FOR (T-379 bulk
    // migration), so each drives an inner IRJob::parallelFor that fans its
    // per-entity work across the whole worker pool. A PARALLEL_FOR system
    // therefore cannot share a multi-system parallel group: its inner
    // parallelFor would be asked to fan out from a worker thread, which
    // SystemManager::validateAllPipelineGroups rejects at boot. (T-332 grouped
    // PERIODIC_IDLE + MODIFIER_DECAY back when both were SERIAL; T-379 tagged
    // them PARALLEL_FOR without splitting the group, which is what aborted this
    // demo at startup.) Per-system data-parallelism supersedes that old
    // task-parallel co-execution. The three trailing systems are SERIAL and
    // ordered as a producer→consumer chain: PERIODIC_IDLE_POSITION_OFFSET reads
    // C_PeriodicIdle (after PERIODIC_IDLE), then PROPAGATE_TRANSFORM and
    // UPDATE_VOXEL_SET_CHILDREN run in sequence on C_WorldTransform.
    IRSystem::registerPipelineGroups(
        IRTime::Events::UPDATE,
        {{IRSystem::createSystem<IRSystem::PERIODIC_IDLE>()},
         {IRSystem::createSystem<IRSystem::MODIFIER_DECAY>()},
         {IRSystem::createSystem<IRSystem::PERIODIC_IDLE_POSITION_OFFSET>()},
         {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()},
         {IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // PERF_STATS_OVERLAY implicitly enables both timing histograms at
    // beginTick (see system_perf_stats_overlay.hpp). The flip runs every
    // frame, so disabling either flag has no effect while PERF_STATS_OVERLAY
    // is in the pipeline; remove the system from the pipeline to disable
    // timing collection.

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            // Hi-Z max-depth mip chain over the (now final) distance texture,
            // for next frame's voxel occlusion cull (#1294 child 1/3). Produces
            // only — renders unchanged this PR.
            IRSystem::createSystem<IRSystem::COMPUTE_DISTANCE_HIZ>(),
            IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::FOG_TO_TRIXEL>(),
        }
    );
    // PERF_STATS_OVERLAY mutates the C_TextSegment of its tracked entity;
    // TEXT_TO_TRIXEL rasterizes the text onto the GUI canvas; the canvas
    // is composited into the framebuffer by TRIXEL_TO_FRAMEBUFFER. Order
    // must be overlay → text → trixel-to-fb for the HUD to land on
    // screen — matches the lighting demo wiring. --no-overlay drops it so a
    // captured frame is deterministic (the HUD's live timing text is otherwise
    // run-variant, defeating a bit-identical cull-on vs cull-off compare).
    if (!g_noOverlay) {
        renderPipeline.push_back(IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>());
    }
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    // #1910 composite-depth probe — registered only with --depth-probe so a
    // flagless run adds no system. Runs after the framebuffer composite and logs
    // the depth-test winner at the requested pixel each frame.
    if (g_cliOverrides.depthProbeSet_) {
        const ivec2 probePixel = g_cliOverrides.depthProbePixel_;
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "DepthProbe",
                [](C_Camera &) {},
                [probePixel]() { IRPrefab::DepthProbe::logCompositeDepth(probePixel); }
            )
        );
    }

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_Name>(
            "AutoProfile",
            [](C_Name &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting", g_autoProfileFrames);
                    // Dump the last completed frame's CPU + GPU per-stage
                    // ms so PR bodies can quote concrete before/after numbers
                    // without screenshotting the HUD. Single-frame value, but
                    // representative at the steady state perf_grid settles
                    // into after warmup.
                    const auto &cpu = IRProfile::cpuFrameHistogram();
                    const auto &gpu = IRRender::gpuStageTiming();
                    IR_LOG_INFO(
                        "Auto-profile stats — FPS:{:.1f} frame:{:.3f}ms",
                        IRTime::renderFps(),
                        IRTime::renderFrameTimeMs()
                    );
                    IR_LOG_INFO(
                        "Auto-profile CPU — voxelStage1:{:.3f} voxelStage2:{:.3f} "
                        "voxelCompact:{:.3f} input:{:.3f} update:{:.3f} render:{:.3f}",
                        cpu.lastFrameMs("voxelStage1"),
                        cpu.lastFrameMs("voxelStage2"),
                        cpu.lastFrameMs("voxelCompact"),
                        cpu.lastFrameMs("input"),
                        cpu.lastFrameMs("update"),
                        cpu.lastFrameMs("render")
                    );
                    // Self-describing per-scope dump: every IR_PROFILE_SCOPE
                    // that ran last frame, sorted by total ms descending. Lets
                    // a profiling pass localize cost inside a system tick
                    // without re-editing this hard-coded list each time.
                    {
                        std::vector<std::pair<std::string_view, double>> scopes;
                        for (const auto &[name, stats] : cpu.lastFrame()) {
                            scopes.emplace_back(name, stats.totalMs_);
                        }
                        std::sort(scopes.begin(), scopes.end(), [](const auto &a, const auto &b) {
                            return a.second > b.second;
                        });
                        for (const auto &[name, ms] : scopes) {
                            IR_LOG_INFO("Auto-profile CPU-scope — {}: {:.3f}ms", name, ms);
                        }
                    }
                    // #2280 sub-stage attribution: canvasClear + voxelCompact +
                    // voxelStage1 (the stage-1 dispatch only now) + voxelStage2
                    // sum to the old bundled voxelStage1 measurement.
                    IR_LOG_INFO(
                        "Auto-profile GPU — canvasClear:{:.3f} voxelCompact:{:.3f} "
                        "voxelStage1:{:.3f} voxelStage2:{:.3f}",
                        gpu.canvasClearMs_,
                        gpu.voxelCompactMs_,
                        gpu.voxelStage1Ms_,
                        gpu.voxelStage2Ms_
                    );
                    IRWindow::closeWindow();
                }
            }
        );
        renderPipeline.push_back(autoProfileId);
    }

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        if (g_cliOverrides.yawRamp_) {
            buildYawRampShots();
            cfg.shots_ = g_yawRampShots.data();
            cfg.numShots_ = static_cast<int>(g_yawRampShots.size());
            // The tiered set steps a few degrees within each cardinal region but
            // jumps ~70° between regions; a generous settle keeps the iterative
            // lighting (light volume / AO / sun-shadow) converged across those
            // jumps and lets the per-axis allocation gate settle to the pose's
            // true render path before capture (see buildYawRampShots above).
            cfg.settleFrames_ = 16;
            // Measure each pose's actual render path at the settled frame.
            cfg.onCaptureFrame_ = &logRampPose;
        } else if (g_settings.mode_ == PerfGridMode::Gallery) {
            cfg.shots_ = kGalleryShots;
            cfg.numShots_ = sizeof(kGalleryShots) / sizeof(kGalleryShots[0]);
            // Same rationale as the yaw ramp: consecutive shots step yaw, so
            // give the iterative lighting time to converge before capture.
            cfg.settleFrames_ = 12;
        } else {
            cfg.shots_ = kShots;
            cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        }
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    createGridEntities();
    configureLightingAndCanvas();
}
