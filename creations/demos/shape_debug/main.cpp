#include <irreden/ir_args.hpp>
#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/asset/voxel_set_format.hpp>
#include <irreden/voxel/dense_bridge.hpp>

#include <array>
#include <cstdio>
#include <numbers>
#include <string>
#include <vector>
// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/camera.hpp>

// SYSTEMS
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_distance_hiz.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_resolve_per_axis_screen_depth.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_debug_culling_minimap.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_auto_yaw_rotate.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/commands/command_toggle_culling_freeze.hpp>
#include <irreden/render/commands/command_toggle_culling_minimap.hpp>

namespace {

// ROI crop tables are framebuffer-pixel coords; values below assume the
// 1280x720 default game resolution (`kGameResolution`). On HiDPI hosts the
// framebuffer is a power-of-two larger and the crops land in the upper-left
// quadrant of the captured image — still useful for edge-fidelity inspection
// at a given fixed offset. Pixel-precise crop placement is intentionally a
// per-host iteration point; refine the coords as the demo's content evolves.
constexpr IRVideo::RoiCrop kCropsZoom4Origin[] = {
    {520, 280, 128, 128, "center_cube_top"},
    {220, 280, 128, 128, "left_cube_silhouette"},
    {820, 280, 128, 128, "right_cube_silhouette"},
};

constexpr IRVideo::RoiCrop kCropsZoom8Origin[] = {
    {520, 280, 128, 128, "center_cube_top"},
    {300, 400, 128, 128, "lower_left_face"},
};

// --pivot-focus-demo (#1921): a tall strip over screen center where the pinned
// pillar sits. With the fix the pillar holds this crop steady across the yaw
// sweep; with --pivot-origin it swings out of frame. Coords are a per-host
// iteration point (see the kCrops* note above). Tuned for the HiDPI 2x
// framebuffer (pillar pivot-pins at frame center ~(1280,720) of 2560x1440).
// Linux/1x smoke: update to ~{490, 100, 300, 520} (center≈(640,360) of
// 1280x720) and re-baseline after confirming the pillar stays centered.
constexpr IRVideo::RoiCrop kCropsPivotPillar[] = {
    {1130, 460, 300, 520, "pivot_pillar_center"},
};

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "zoom1_origin"},
    {2.0f, vec2(0, 0), 0.0f, "zoom2_origin"},
    {4.0f,
     vec2(0, 0),
     0.0f,
     "zoom4_origin",
     kCropsZoom4Origin,
     sizeof(kCropsZoom4Origin) / sizeof(kCropsZoom4Origin[0])},
    {1.0f, vec2(1, 0), 0.0f, "zoom1_odd_offset"},
    {8.0f,
     vec2(0, 0),
     0.0f,
     "zoom8_origin",
     kCropsZoom8Origin,
     sizeof(kCropsZoom8Origin) / sizeof(kCropsZoom8Origin[0])},
    {4.0f, vec2(3, 5), 0.0f, "zoom4_offset_3_5"},
    // LOD swap (#1467): the co-located stack renders exactly ONE variant per
    // zoom band — cube (zoom 1-3) -> cone (zoom 4-15) -> sphere (zoom >=16) —
    // never stacked. This whole-scene shot samples the finest (sphere) tier.
    {16.0f, vec2(0, 0), 0.0f, "zoom16_lod_fine_sphere"},

    // Dedicated LOD swap series (#1467): cameraIso (16,-16) centers the
    // co-located LOD stack at world (0,-16,0) (= -pos3DtoPos2DIso((0,-16,0))),
    // with the single-LOD control cylinder beside it. As zoom climbs the stack
    // visibly swaps silhouette (cube -> cone -> sphere) while the control holds
    // constant — the unambiguous "LOD is working" read the issue asks for.
    {1.0f, vec2(16, -16), 0.0f, "lod_swap_zoom1_cube"},
    {2.0f, vec2(16, -16), 0.0f, "lod_swap_zoom2_cube"},
    {4.0f, vec2(16, -16), 0.0f, "lod_swap_zoom4_cone"},
    {8.0f, vec2(16, -16), 0.0f, "lod_swap_zoom8_cone"},
    {16.0f, vec2(16, -16), 0.0f, "lod_swap_zoom16_sphere"},

    // Rotation coverage (#1261): four cardinals + one inter-cardinal expose
    // rotation-only regressions (#1256 checkerboard, #1257 inter-cardinal
    // deformation, future face-normal / shadow-AABB / chunk-mask bugs) that
    // a yaw=0 shot list cannot catch. zoom8 close-ups make sub-pixel
    // parity artifacts visible at full pixel scale.
    {4.0f, vec2(0, 0), IRMath::kHalfPi, "zoom4_yaw90"},
    {4.0f, vec2(0, 0), IRMath::kPi, "zoom4_yaw180"},
    {4.0f, vec2(0, 0), 3.0f * IRMath::kHalfPi, "zoom4_yaw270"},
    {4.0f, vec2(0, 0), IRMath::kQuarterPi, "zoom4_yaw45_inter_cardinal"},
    {8.0f,
     vec2(0, 0),
     IRMath::kPi,
     "zoom8_yaw180",
     kCropsZoom8Origin,
     sizeof(kCropsZoom8Origin) / sizeof(kCropsZoom8Origin[0])},

    // Camera-focus pivot coverage (#1352): the camera is panned off-origin and
    // then yawed. With RotationPivotMode::CAMERA_CENTER (the new engine default)
    // the world point under screen center stays pinned across the whole sweep —
    // the same focused content sits at the same screen pixel in all four shots.
    // The pre-#1352 ORIGIN pivot (reproducible via --pivot-origin) instead swings
    // that content in an arc. A shot list with only unpanned-or-yaw0 entries
    // cannot catch a pivot regression: the CAMERA_CENTER correction is the
    // identity unless the camera is BOTH panned and rotated, so every existing
    // shot is byte-identical between the two modes. yaw45 also exercises the
    // per-axis smooth-yaw scatter base (perAxisBase_) under the pivot.
    {4.0f, vec2(16, 16), 0.0f, "zoom4_pan16_yaw0_pivot"},
    {4.0f, vec2(16, 16), IRMath::kHalfPi, "zoom4_pan16_yaw90_pivot"},
    {4.0f, vec2(16, 16), IRMath::kPi, "zoom4_pan16_yaw180_pivot"},
    {4.0f, vec2(16, 16), IRMath::kQuarterPi, "zoom4_pan16_yaw45_pivot"},
};

int g_autoWarmupFrames = 0; // 0 = --auto-screenshot not requested
bool g_depthColor = false;
bool g_checkerboard = false; // opt-in via --checkerboard; flickered, off by default
// --occlusion-cull (#1294 child 2/3): force the voxel-pool chunk-occlusion HZB
// pre-pass on (off by default in the engine). A test hook so the cull can be
// exercised before the child-3 measurement demo lands. On the sparse shape_debug
// scene nothing is inter-object-occluded, so output must stay identical (a hole
// would be a false-positive cull bug).
bool g_occlusionCull = false;
// --gpu-voxel-smoke (#1396): spawn one voxel cube routed through the GPU
// voxel-position prepass with a fixed 45° rotation. Off by default so the
// standard scene stays byte-identical; the rotated cube is direct proof the
// prepass applied modelToWorld (the CPU world-position path is translation-only).
bool g_gpuVoxelSmoke = false;
// --skin-smoke (#1605): spawn one 2-bone rigged voxel bar skinned through the
// binding-17 bone→slot seed path, with the second joint posed off its bind
// pose. Off by default so the standard scene stays byte-identical; the visible
// bend at the bar's midpoint is direct proof the per-voxel slots route through
// the joint skin matrices (a rigid entity transform cannot bend a set).
bool g_skinSmoke = false;
int g_autoProfileFrames = 0; // 0 = disabled
int g_autoProfileCount = 0;
float g_initialZoom = 0.0f; // 0 = use engine default
float g_initialYawRadians = 0.0f;
float g_initialYaw = 0.0f;
bool g_initialYawSet = false;
// --pivot-origin (#1352): force RotationPivotMode::ORIGIN (the pre-#1352
// world-origin pivot) instead of the CAMERA_CENTER engine default. Lets the
// same panned+rotated shot list be captured in both modes for an A/B compare —
// CAMERA_CENTER pins the focused content at screen center, ORIGIN swings it in
// an arc. Off by default so the demo exercises the shipped default.
bool g_pivotOrigin = false;
IRRender::DebugOverlayMode g_debugOverlay = IRRender::DebugOverlayMode::NONE;
// --load-vxs <path>: load a DENSE-mode .vxs and render frame 0 alongside the
// built-in shape fixtures. Empty = not requested.
std::string g_loadVxsPath;

// --spin-yaw [deg/sec] (#1271): drive the camera's Z-yaw at a constant
// rate so the cardinal/residual rebracket can be eyeballed (live) or sampled
// at N evenly-spaced angles (auto-screenshot). 0 = flag not requested.
float g_spinYawDegPerSec = 0.0f;
// In auto-screenshot + spin-yaw mode, --auto-screenshot's value is
// reinterpreted as "shot count across one rotation"; default 24 → every 15°
// which hits every cardinal (0/90/180/270°) and every rebracket (45/135/...).
int g_spinYawShotCount = 24;

// --spin-shape <name> (#1922): spawn a single shape centred at the origin (so
// camera Z-yaw-about-origin keeps it screen-centred) instead of the full
// side-by-side fixture scene — the per-shape isolation the temporal-jitter
// sweep harness (scripts/dev/shape-rotate-jitter-sweep) needs. Empty = full
// scene (default, byte-identical). --spin-shape-voxel renders the voxel-pool
// twin instead of the SDF solver, so both render paths can be scored.
std::string g_spinShapeType;
bool g_spinShapeVoxel = false;
// Centred ROI crop attached to every --spin-shape sweep shot so the jitter
// metric decodes a small PNG per frame — a full-framebuffer 100+-frame sweep
// is otherwise minutes of pure-Python PNG decode. Sized from the live
// framebuffer at sweep-build time; one shared crop suffices because the single
// shape stays screen-centred under yaw-about-origin.
IRVideo::RoiCrop g_spinShapeCrop{};

// Dynamic shot table populated at startup when --spin-yaw + --auto-screenshot
// are both set. Lives at namespace scope so the pointer handed to
// IRVideo::AutoScreenshotConfig outlives the game loop. The label strings
// are backed by a parallel buffer so each AutoScreenshotShot::label_ pointer
// remains stable.
std::vector<IRVideo::AutoScreenshotShot> g_spinYawShots;
std::vector<std::array<char, 32>> g_spinYawShotLabels;

// --cull-validate (#1438): frozen-cull free-fly validation harness. Requires
// --auto-screenshot. Builds a paired live/frozen camera sweep — a live
// (cull-tracking) pass followed by a frozen pass over the SAME poses with the
// cull pinned at a deliberately wide reference. Pairwise-diffing the on-screen
// region of cv_live_NNN vs cv_frozen_NNN proves the live cull never drops
// on-screen content under yaw + camera movement. Same stable-storage discipline
// as the spin-yaw buffers above.
bool g_cullValidate = false;
std::vector<IRVideo::AutoScreenshotShot> g_cullValidateShots;
std::vector<std::array<char, 40>> g_cullValidateShotLabels;

// --pivot-focus-demo (#1921): spawn a tall, off-center voxel pillar and drive a
// yaw sweep that pins the camera Z-yaw pivot on the pillar's TRUE-depth center
// via AutoScreenshotShot::pivotFocusWorld_. With the fix the pillar rotates in
// place — it stays at screen center across the whole sweep, including its z>0
// body. Add --pivot-origin for the A/B where the legacy z=0 screen-center pivot
// swings it in an arc. Requires --auto-screenshot. Same stable-storage
// discipline as the buffers above.
bool g_pivotFocusDemo = false;
std::vector<IRVideo::AutoScreenshotShot> g_pivotFocusShots;
std::vector<std::array<char, 40>> g_pivotFocusShotLabels;
// World center of the --pivot-focus-demo pillar. Off the world origin in x/y AND
// at z > 0, so the legacy pivot exhibits BOTH defects (#1921): the z=0 focus the
// old path picks lands horizontally offset from a centered tall column, and an
// off-origin column orbits the origin. Shared by the spawn and the shot table so
// the focus exactly matches the rendered geometry.
constexpr vec3 kPivotPillarCenter = vec3(8.0f, -8.0f, 10.0f);

// --pan-sweep (#1944 diagnosis): hold yaw + zoom fixed and step the camera iso
// position in fine sub-trixel increments across ~2 trixels, capturing one frame
// per step. A static scene must translate SMOOTHLY across the sweep — any
// per-frame +/-1px oscillation as the camera crosses a trixel boundary is the
// anti-vibration jitter (the sub-pixel decomposition disagreeing with the
// integer canvas placement). Defaults to yaw 45 deg (residual != 0 → per-axis
// composite active) so it exercises rotation; override with --yaw. Requires
// --auto-screenshot (its value sets the step count, min 2).
bool g_panSweep = false;
std::vector<IRVideo::AutoScreenshotShot> g_panSweepShots;
std::vector<std::array<char, 40>> g_panSweepShotLabels;

// --yaw-sweep (#1944 diagnosis): hold camera position + zoom fixed and step the
// camera Z-yaw in fine increments, capturing one frame per step. The companion
// to --pan-sweep: it exercises jitter during ROTATION (the effective camera iso
// changes via the RotationPivotMode drift-cancel as yaw advances, so the per-axis
// composite's camera-offset decomposition is swept). The range stays inside ONE
// cardinal quadrant (residual yaw 0..π/4, constant visible-face triplet) so a
// Z-yaw-invariant probe (a vertical cylinder) has a stable silhouette and any
// per-frame centroid wobble is jitter, not a face-triplet flip. Requires
// --auto-screenshot.
bool g_yawSweep = false;
std::vector<IRVideo::AutoScreenshotShot> g_yawSweepShots;
std::vector<std::array<char, 40>> g_yawSweepShotLabels;

// Register shape_debug's custom flags on the engine-owned parser. --help /
// --auto-screenshot / --config-preset are pre-registered by the Parser ctor;
// IREngine::init(argc, argv) parses common + these in one pass, so --help lists
// every flag and exits before any window/GL/Metal init (epic #2057 P3, #2060).
void registerCliArgs() {
    IRArgs::Parser &args = IREngine::args();
    args.optionalInt(
        "--auto-profile",
        "Run N frames (default 300) with frame timing, then exit",
        300
    );
    args.flag("--depth-color", "Tint each voxel by local iso-depth (front=red, back=blue)");
    args.flag("--checkerboard", "Tint alternating voxels darker (flickers; off by default)");
    args.flag(
        "--occlusion-cull",
        "Force the voxel-pool chunk-occlusion HZB pre-pass ON (off by default)"
    );
    args.flag("--gpu-voxel-smoke", "Spawn one cube routed through the GPU voxel-position prepass");
    args.flag("--pivot-focus-demo", "Yaw sweep pinning the pivot on a tall pillar (#1921)");
    args.flag("--skin-smoke", "Spawn one 2-bone rigged voxel bar skinned via binding-17 (#1605)");
    args.flag("--pan-sweep", "Fine fixed-yaw camera-pan jitter sweep (#1944)");
    args.flag("--yaw-sweep", "Fine fixed-position camera-yaw jitter sweep (#1944)");
    args.number("--zoom", "Initial camera zoom (snapped to nearest power of two)", 0.0f);
    args.string("--debug-overlay", "Debug overlay mode (e.g. none, depth, normals)", "none");
    args.number("--yaw", "Initial camera Z-yaw in radians", 0.0f);
    args.flag("--pivot-origin", "Force the legacy world-origin Z-yaw pivot (#1352 A/B)");
    args.flag("--cull-validate", "Frozen-cull free-fly validation sweep (#1438)");
    args.string(
        "--load-vxs",
        "Path to a DENSE-mode .vxs to load and render alongside fixtures",
        ""
    );
    args.optionalInt(
        "--spin-yaw",
        "Drive camera Z-yaw (deg/sec live, default 30; shot-count across one rotation when "
        "combined with --auto-screenshot)",
        30
    );
    args.string(
        "--spin-shape",
        "Spawn a single named shape centred at origin instead of the full fixture scene (#1922)",
        ""
    );
    args.flag("--spin-shape-voxel", "Render the --spin-shape via the voxel-pool twin, not the SDF");
}

// Read the parsed values back into the demo's globals. Runs AFTER
// IREngine::init(argc, argv) has parsed. A value flag only writes its global
// when actually provided, preserving each global's pre-parse default.
void readCliArgs() {
    const IRArgs::Parser &args = IREngine::args();

    g_autoWarmupFrames = args.autoScreenshotWarmupFrames();

    // --auto-profile: 0 when absent, else the frame count (300 if bare).
    if (args.wasProvided("--auto-profile")) {
        g_autoProfileFrames = args.getInt("--auto-profile");
    }
    g_depthColor = args.getFlag("--depth-color");
    g_checkerboard = args.getFlag("--checkerboard");
    g_occlusionCull = args.getFlag("--occlusion-cull");
    g_gpuVoxelSmoke = args.getFlag("--gpu-voxel-smoke");
    g_pivotFocusDemo = args.getFlag("--pivot-focus-demo");
    g_skinSmoke = args.getFlag("--skin-smoke");
    g_panSweep = args.getFlag("--pan-sweep");
    g_yawSweep = args.getFlag("--yaw-sweep");

    if (args.wasProvided("--zoom")) {
        const float zoom = args.getFloat("--zoom");
        if (zoom > 0.0f) {
            g_initialZoom = zoom;
        }
    }
    if (args.wasProvided("--debug-overlay")) {
        g_debugOverlay =
            IRRender::debugOverlayModeFromString(args.getString("--debug-overlay").c_str());
    }
    if (args.wasProvided("--yaw")) {
        const float yaw = args.getFloat("--yaw");
        g_initialYawRadians = yaw;
        g_initialYaw = yaw;
        g_initialYawSet = true;
    }
    g_pivotOrigin = args.getFlag("--pivot-origin");
    g_cullValidate = args.getFlag("--cull-validate");
    if (args.wasProvided("--load-vxs")) {
        g_loadVxsPath = args.getString("--load-vxs");
    }
    // --spin-yaw: 0 (disabled) when absent, else the rate (30 if bare). The
    // optional value reads as an int — fractional deg/sec is truncated.
    if (args.wasProvided("--spin-yaw")) {
        g_spinYawDegPerSec = static_cast<float>(args.getInt("--spin-yaw"));
    }
    if (args.wasProvided("--spin-shape")) {
        g_spinShapeType = args.getString("--spin-shape");
    }
    g_spinShapeVoxel = args.getFlag("--spin-shape-voxel");
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

    // --spin-yaw + --auto-screenshot: reinterpret the screenshot value as
    // "shots across one rotation", and use a small internal warmup. This is
    // the regression-detection mode — N static frames at evenly-spaced yaws
    // so a render-verify diff can pinpoint the angle a glitch appears at.
    if (g_spinYawDegPerSec > 0.0f && g_autoWarmupFrames > 0) {
        g_spinYawShotCount = g_autoWarmupFrames;
        g_autoWarmupFrames = 10;
        IR_LOG_INFO(
            "Spin-yaw: warmup reset to 10 frames (--auto-screenshot value {} reinterpreted as shot "
            "count)",
            g_spinYawShotCount
        );
    }

    IR_LOG_INFO("Starting creation: shape_debug");
    if (g_autoProfileFrames > 0) {
        IREngine::enableFrameTiming(true);
    }
    initSystems();
    initCommands();
    initEntities();
    if (g_initialZoom > 0.0f) {
        IRRender::setCameraZoom(g_initialZoom);
        vec2 actualZoom = IRRender::getCameraZoom();
        IR_LOG_INFO(
            "Initial zoom: requested={}, actual={} (snapped to nearest power of two)",
            g_initialZoom,
            actualZoom.x
        );
    }
    if (g_debugOverlay != IRRender::DebugOverlayMode::NONE) {
        IRRender::setDebugOverlay(g_debugOverlay);
    }
    if (g_occlusionCull) {
        IRRender::setVoxelOcclusionCullEnabled(true);
        IR_LOG_INFO("Voxel chunk-occlusion cull forced ON (--occlusion-cull, #1294 child 2/3)");
    }
    if (g_initialYawRadians != 0.0f) {
        IRPrefab::Camera::setYaw(g_initialYawRadians);
        IR_LOG_INFO(
            "Initial camera Z-yaw: {} rad ({} deg)",
            g_initialYawRadians,
            g_initialYawRadians * (180.0f / std::numbers::pi_v<float>)
        );
    }
    if (g_initialYawSet) {
        IRPrefab::Camera::setYaw(g_initialYaw);
        IR_LOG_INFO("Initial yaw: {} rad", g_initialYaw);
    }
    if (g_pivotOrigin) {
        IRRender::setRotationPivotMode(IRRender::RotationPivotMode::ORIGIN);
        IR_LOG_INFO(
            "RotationPivotMode: ORIGIN (--pivot-origin) — Z-yaw pivots about the world origin"
        );
    }
    if (g_cullValidate) {
        // The cull viewport also drives the sun-shadow-feeder AABB
        // (shadowFeederCullViewport), so a frozen wide cull bakes *different*
        // shadows than the live cull — a confound that otherwise dominates the
        // live-vs-frozen diff. Disabling sun shadows leaves the cull's only
        // on-screen effect as which voxels rasterize, so the diff isolates
        // voxel retention. (The interactive F10 path keeps shadows.)
        IRRender::setSunShadowsEnabled(false);
        IR_LOG_INFO(
            "Cull-validate: sun shadows disabled to isolate voxel retention from "
            "shadow-feeder coupling"
        );
    }
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::LOD_UPDATE>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    // --spin-yaw live mode: drive the camera each frame. In auto-screenshot
    // mode the per-shot setYaw() supplies the rotation instead — running both
    // would double-rotate between shots and break the "evenly-spaced" contract.
    if (g_spinYawDegPerSec > 0.0f &&
        g_autoWarmupFrames == 0) { // == 0: no auto-screenshot requested
        const float radPerFrame =
            g_spinYawDegPerSec * IRMath::kPi / 180.0f / static_cast<float>(IRConstants::kFPS);
        renderPipeline.push_front(IRSystem::createSystem<IRSystem::AUTO_YAW_ROTATE>(radPerFrame));
        IR_LOG_INFO(
            "Spin-yaw live: {} deg/sec ({} rad/frame at {} fps)",
            g_spinYawDegPerSec,
            radPerFrame,
            IRConstants::kFPS
        );
    }
    // GPU voxel-position prepass (#1396) — writes binding 5 for
    // GPU-transform-indirected voxel sets before STAGE_1 reads it. A no-op (no
    // dispatch) unless a voxel set opts in via gpuTransformSlot_, so the default
    // scene stays byte-identical. Created up-front so its SystemId can wire the
    // transform-slot allocator; it keeps its pipeline position below (before
    // STAGE_1).
    const IRSystem::SystemId updateVoxelPositionsId =
        IRSystem::createSystem<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>();
    IRPrefab::VoxelTransform::setAllocatorSystem(updateVoxelPositionsId);
    // Joint skin-matrix upload (#1603) + per-voxel bone→slot seeding (#1605).
    // Before UPDATE_VOXEL_POSITIONS_GPU so binding 18 holds the skin matrices
    // when the prepass dispatches; a no-op (zero skeletons) unless --skin-smoke
    // rigs a set, so the default scene stays byte-identical.
    const IRSystem::SystemId updateJointMatricesId =
        IRSystem::createSystem<IRSystem::UPDATE_JOINT_MATRICES>();
    IRPrefab::JointTransform::setSystem(updateJointMatricesId);
    // Captured for the culling minimap's light + caster domains (#2316, V2) —
    // the minimap reads these systems' per-frame gather state back rather
    // than re-running its own light/caster query. Assigned in-place inside
    // the initializer list below (NOT hoisted out as their own create()
    // calls) — a braced-init-list evaluates left-to-right, and
    // BAKE_SUN_SHADOW_MAP::create() depends on a named resource
    // RESOLVE_PER_AXIS_SCREEN_DEPTH::create() creates earlier in this same
    // list; calling create() early breaks that ordering.
    IRSystem::SystemId bakeSunShadowMapId{};
    IRSystem::SystemId computeLightVolumeId{};
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
            updateJointMatricesId,
            updateVoxelPositionsId,
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
            // Hi-Z max-depth mip chain over the (now final) distance texture,
            // for next frame's voxel occlusion cull (#1294 child 1/3). Produces
            // only — renders unchanged this PR.
            IRSystem::createSystem<IRSystem::COMPUTE_DISTANCE_HIZ>(),
            IRSystem::createSystem<IRSystem::RESOLVE_PER_AXIS_SCREEN_DEPTH>(),
            (bakeSunShadowMapId = IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>()),
            IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
            (computeLightVolumeId = IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>()),
            IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::System<IRSystem::DEBUG_CULLING_MINIMAP>::create({
                .lightVolumeSystemId_ = computeLightVolumeId,
                .bakeSunShadowSystemId_ = bakeSunShadowMapId,
            }),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );
    // Off during --auto-screenshot captures — the minimap is a live debug
    // aid, not part of the render-verify golden image (#2316, V2 plan
    // "Verification": map off during reference captures). Interactive /
    // --auto-profile runs (g_autoWarmupFrames == 0) default it visible;
    // F11 (initCommands below) toggles it either way.
    IRRender::setCullingMinimapEnabled(g_autoWarmupFrames == 0);

    if (g_autoProfileFrames > 0) {
        IRSystem::SystemId autoProfileId = IRSystem::createSystem<C_VoxelSetNew>(
            "AutoProfile",
            [](C_VoxelSetNew &) {},
            []() {
                ++g_autoProfileCount;
                if (g_autoProfileCount >= g_autoProfileFrames) {
                    IR_LOG_INFO("Auto-profile: {} frames collected, exiting", g_autoProfileFrames);
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
        if (g_cullValidate) {
            // Frozen-cull free-fly validation harness (#1438). Two phases over
            // the SAME pose list: (1) live cull, (2) cull frozen at a wide
            // reference. A wide freeze reference (zoom 1 at origin) has an iso
            // cull viewport that is a superset of every sweep pose, so a frozen
            // frame is the "cull-effectively-disabled" ground truth — every
            // voxel geometrically on-screen at that pose is drawn. Pairwise-
            // diffing the on-screen region of cv_live_NNN against cv_frozen_NNN
            // proves the live cull never drops on-screen content. Capture order
            // makes screenshot_i (live i) pair with screenshot_(i + n + 1)
            // (frozen i); the freeze-ref shot sits between the two phases.
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            constexpr int kYawSteps = 8;
            const vec2 kPanOffsets[] = {vec2(12, 0), vec2(-12, 0), vec2(0, 12), vec2(0, -12)};
            constexpr int kNumPans = sizeof(kPanOffsets) / sizeof(kPanOffsets[0]);
            const int posesPerPhase = kYawSteps + kNumPans;
            // live poses + freeze-ref + frozen poses + unfreeze
            const int totalShots = posesPerPhase * 2 + 2;
            g_cullValidateShotLabels.reserve(totalShots);
            g_cullValidateShots.reserve(totalShots);

            const auto emitShot = [&](float zoom,
                                      vec2 cam,
                                      float yaw,
                                      const char *fmt,
                                      int idx,
                                      IRVideo::CullAction action) {
                auto &label = g_cullValidateShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), fmt, idx);
                IRVideo::AutoScreenshotShot shot{};
                shot.zoom_ = zoom;
                shot.cameraIso_ = cam;
                shot.yawRadians_ = yaw;
                shot.label_ = label.data();
                shot.cullAction_ = action;
                g_cullValidateShots.push_back(shot);
            };

            // The pose list: a full yaw rotation at the focus, then a pan sweep
            // at a non-cardinal yaw (residual != 0 → per-axis composite active)
            // so camera *movement* under rotation is exercised, not just
            // rotation in place.
            const auto emitPhase = [&](const char *fmt) {
                for (int i = 0; i < kYawSteps; ++i) {
                    const float yaw =
                        (static_cast<float>(i) / static_cast<float>(kYawSteps)) * IRMath::kTwoPi;
                    emitShot(sweepZoom, vec2(0, 0), yaw, fmt, i, IRVideo::CullAction::NONE);
                }
                for (int i = 0; i < kNumPans; ++i) {
                    emitShot(
                        sweepZoom,
                        kPanOffsets[i],
                        IRMath::kQuarterPi,
                        fmt,
                        kYawSteps + i,
                        IRVideo::CullAction::NONE
                    );
                }
            };

            emitPhase("cv_live_%03d");
            // Freeze the cull at the wide reference between the two phases.
            emitShot(1.0f, vec2(0, 0), 0.0f, "cv_freeze_ref_%03d", 0, IRVideo::CullAction::FREEZE);
            emitPhase("cv_frozen_%03d");
            // Release the freeze so the harness leaves global state clean.
            emitShot(
                sweepZoom,
                vec2(0, 0),
                0.0f,
                "cv_unfreeze_%03d",
                0,
                IRVideo::CullAction::UNFREEZE
            );

            cfg.shots_ = g_cullValidateShots.data();
            cfg.numShots_ = static_cast<int>(g_cullValidateShots.size());
            IR_LOG_INFO(
                "Cull-validate sweep: {} poses/phase, {} total shots (live + frozen) at zoom={}",
                posesPerPhase,
                cfg.numShots_,
                sweepZoom
            );
        } else if (g_spinYawDegPerSec > 0.0f) {
            // Sweep one full rotation at camera=(0,0). Default zoom=4 matches
            // the rotation-coverage shots (#1261) for scene-scale smoothness;
            // pass --zoom to sweep at high zoom (e.g. 16), where rotation-only
            // parity glitches (#1218 black faces, #1256 checkerboard) are
            // visible at full pixel scale. The regression set baselines both.
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            const int n = IRMath::max(2, g_spinYawShotCount);
            // Reserve up front so push_back never reallocates — moving the
            // label buffer would invalidate the pointers already in
            // g_spinYawShots.
            g_spinYawShotLabels.reserve(n);
            g_spinYawShots.reserve(n);
            // In --spin-shape mode the single shape is screen-centred under
            // yaw-about-origin, so attach a centred ROI crop sized to half the
            // shorter framebuffer edge: the jitter sweep then scores small
            // per-frame PNGs instead of the full retina framebuffer.
            const bool useSpinShapeCrop = !g_spinShapeType.empty();
            if (useSpinShapeCrop) {
                ivec2 fb{0, 0};
                IRWindow::getFramebufferSize(fb);
                const int side = IRMath::max(64, IRMath::min(fb.x, fb.y) / 2);
                g_spinShapeCrop = {(fb.x - side) / 2, (fb.y - side) / 2, side, side, "center"};
            }
            for (int i = 0; i < n; ++i) {
                const float yaw = (static_cast<float>(i) / static_cast<float>(n)) * IRMath::kTwoPi;
                auto &label = g_spinYawShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "spin_yaw_%03d_of_%03d", i, n);
                IRVideo::AutoScreenshotShot shot{sweepZoom, vec2(0, 0), yaw, label.data()};
                if (useSpinShapeCrop) {
                    shot.crops_ = &g_spinShapeCrop;
                    shot.numCrops_ = 1;
                }
                g_spinYawShots.push_back(shot);
            }
            cfg.shots_ = g_spinYawShots.data();
            cfg.numShots_ = static_cast<int>(g_spinYawShots.size());
            IR_LOG_INFO(
                "Spin-yaw sweep: {} shots across one rotation at zoom={}",
                cfg.numShots_,
                sweepZoom
            );
        } else if (g_pivotFocusDemo) {
            // Pin the camera Z-yaw pivot on the tall pillar's true-depth center
            // across a yaw sweep (#1921). centerPan brings the pillar to screen
            // center at yaw 0; with the focus set it stays there for EVERY yaw —
            // the pillar rotates in place while the default scene sweeps around
            // it. Run the same flag with --pivot-origin for the A/B: the legacy
            // z=0 screen-center pivot swings the pillar out in an arc.
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            // Pan so the pillar lands at screen center at yaw 0: the producers
            // place content at screen `iso(W) + cameraIso`, so centering the
            // pillar (`screen == 0`) needs `cameraIso == -iso(pillarCenter)`.
            const vec2 centerPan = -IRMath::pos3DtoPos2DIso(kPivotPillarCenter);
            const float yaws[] =
                {0.0f, IRMath::kHalfPi, IRMath::kPi, 3.0f * IRMath::kHalfPi, IRMath::kQuarterPi};
            constexpr int n = sizeof(yaws) / sizeof(yaws[0]);
            g_pivotFocusShotLabels.reserve(n);
            g_pivotFocusShots.reserve(n);
            for (int i = 0; i < n; ++i) {
                auto &label = g_pivotFocusShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "pivot_focus_yaw_%03d", i);
                IRVideo::AutoScreenshotShot shot{};
                shot.zoom_ = sweepZoom;
                shot.cameraIso_ = centerPan;
                shot.yawRadians_ = yaws[i];
                shot.label_ = label.data();
                shot.crops_ = kCropsPivotPillar;
                shot.numCrops_ = sizeof(kCropsPivotPillar) / sizeof(kCropsPivotPillar[0]);
                shot.pivotFocusWorld_ = kPivotPillarCenter;
                shot.hasPivotFocus_ = true;
                g_pivotFocusShots.push_back(shot);
            }
            cfg.shots_ = g_pivotFocusShots.data();
            cfg.numShots_ = static_cast<int>(g_pivotFocusShots.size());
            IR_LOG_INFO(
                "Pivot-focus demo: {} yaw shots pinned on pillar ({}, {}, {}) at zoom={}",
                cfg.numShots_,
                kPivotPillarCenter.x,
                kPivotPillarCenter.y,
                kPivotPillarCenter.z,
                sweepZoom
            );
        } else if (g_panSweep) {
            // Fine pan sweep at a FIXED yaw (#1944 jitter diagnosis). Steps the
            // camera iso across 2 trixels in X so the integer game-px part of the
            // anti-vibration decomposition ticks twice; a correct pipeline
            // translates the scene smoothly, a broken one oscillates +/-1px at
            // each tick. yaw defaults to 45 deg (per-axis composite active).
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            const float sweepYaw = g_initialYawSet ? g_initialYaw : IRMath::kQuarterPi;
            const int n = IRMath::max(2, g_autoWarmupFrames > 0 ? g_spinYawShotCount : 24);
            const vec2 base = vec2(16.0f, 16.0f);
            g_panSweepShotLabels.reserve(n);
            g_panSweepShots.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n - 1);
                auto &label = g_panSweepShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "pan_sweep_%03d_of_%03d", i, n);
                IRVideo::AutoScreenshotShot shot{};
                shot.zoom_ = sweepZoom;
                shot.cameraIso_ = base + vec2(2.0f * t, 0.0f);
                shot.yawRadians_ = sweepYaw;
                shot.label_ = label.data();
                g_panSweepShots.push_back(shot);
            }
            cfg.shots_ = g_panSweepShots.data();
            cfg.numShots_ = static_cast<int>(g_panSweepShots.size());
            IR_LOG_INFO(
                "Pan-sweep: {} shots, cameraIso ({},{})->({},{}) at yaw={} rad zoom={}",
                cfg.numShots_,
                base.x,
                base.y,
                base.x + 2.0f,
                base.y,
                sweepYaw,
                sweepZoom
            );
        } else if (g_yawSweep) {
            // Fine yaw sweep at FIXED camera position (#1944 jitter diagnosis,
            // rotation half). Steps yaw across [0.05, 0.70] rad — inside the first
            // cardinal quadrant (< π/4 ≈ 0.785), so the per-axis visible-face
            // triplet is constant and a vertical cylinder probe's silhouette is
            // Z-yaw-invariant: smooth centroid = pass, oscillation = jitter.
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            const int n = IRMath::max(2, 24);
            constexpr float kYawLo = 0.05f;
            constexpr float kYawHi = 0.70f;
            g_yawSweepShotLabels.reserve(n);
            g_yawSweepShots.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(n - 1);
                auto &label = g_yawSweepShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "yaw_sweep_%03d_of_%03d", i, n);
                IRVideo::AutoScreenshotShot shot{};
                shot.zoom_ = sweepZoom;
                shot.cameraIso_ = vec2(0.0f, 0.0f);
                shot.yawRadians_ = kYawLo + (kYawHi - kYawLo) * t;
                shot.label_ = label.data();
                g_yawSweepShots.push_back(shot);
            }
            cfg.shots_ = g_yawSweepShots.data();
            cfg.numShots_ = static_cast<int>(g_yawSweepShots.size());
            IR_LOG_INFO(
                "Yaw-sweep: {} shots, yaw {}->{} rad at cameraIso (0,0) zoom={}",
                cfg.numShots_,
                kYawLo,
                kYawHi,
                sweepZoom
            );
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
    // Interactive cull-freeze toggle (#1438): freeze the cull viewport at the
    // current camera pose, then free-fly (WASD pan / mouse drag / scroll zoom,
    // all from standardControlSystems) to see exactly what the frozen cull
    // retains as the camera moves. F10 matches the other demos' binding.
    IRCommand::createCommand<IRCommand::TOGGLE_CULLING_FREEZE>(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonF10
    );
    // Culling-minimap visibility toggle (#2316, V2). F11 sits next to F10's
    // freeze toggle — the two are commonly used together (freeze, then
    // inspect the minimap's light/caster domains while free-flying).
    IRCommand::createCommand<IRCommand::TOGGLE_CULLING_MINIMAP>(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonF11
    );
}

void applyCheckerboard(C_VoxelSetNew &voxelSet, Color baseColor) {
    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0)
            continue;
        ivec3 cellPos = ivec3(glm::round(voxelSet.positions_[i].pos_));
        Color c = baseColor;
        if (((cellPos.x + cellPos.y + cellPos.z) & 1) != 0) {
            c.red_ = static_cast<std::uint8_t>(c.red_ * 0.55f);
            c.green_ = static_cast<std::uint8_t>(c.green_ * 0.55f);
            c.blue_ = static_cast<std::uint8_t>(c.blue_ * 0.55f);
        }
        voxelSet.voxels_[i].color_ = c;
    }
}

// Bounding half-extent mirroring the GPU formula in
// system_shapes_to_trixel.hpp / c_shapes_to_trixel.glsl, so that CPU depth
// normalization uses the exact same range as the GPU shader.
// Classic HSV->RGB (h,s,v in [0,1]) matching the shader's hsvToRgb helper.
vec3 hsvToRgbCpu(vec3 c) {
    const vec4 K(1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f);
    vec3 p = glm::abs(glm::fract(vec3(c.x) + vec3(K)) * 6.0f - vec3(K.w));
    return c.z * glm::mix(vec3(K.x), glm::clamp(p - vec3(K.x), 0.0f, 1.0f), c.y);
}

// Color each active voxel by its LOCAL iso-depth (x+y+z), normalized to
// [0,1] across the shape's bounding dExtent.  Matches the GPU depth-color
// path in c_shapes_to_trixel.glsl exactly, so the voxel-pool mirror is
// indistinguishable from the SDF render.
void applyDepthColor(C_VoxelSetNew &voxelSet, IRRender::ShapeType type, vec4 sdfParams) {
    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec3 boundingHalf = IRMath::SDF::boundingHalf(sdfType, sdfParams);
    // Match GPU: in iso camera convention, smaller d = closer, so visible
    // window is [-dColor, +dColor/3] and front → t=0 (red), back → t=1.
    float dColor = boundingHalf.x + boundingHalf.y + boundingHalf.z;
    float denom = IRMath::max((4.0f / 3.0f) * dColor, 1.0f);

    for (int i = 0; i < voxelSet.numVoxels_; ++i) {
        if (voxelSet.voxels_[i].color_.alpha_ == 0)
            continue;
        ivec3 cellPos = ivec3(glm::round(voxelSet.positions_[i].pos_));
        float d = static_cast<float>(cellPos.x + cellPos.y + cellPos.z);
        float t = glm::clamp((d + dColor) / denom, 0.0f, 1.0f);
        vec3 rgb = hsvToRgbCpu(vec3(0.66f * t, 1.0f, 1.0f));
        Color c{
            static_cast<std::uint8_t>(glm::clamp(rgb.x, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(rgb.y, 0.0f, 1.0f) * 255.0f),
            static_cast<std::uint8_t>(glm::clamp(rgb.z, 0.0f, 1.0f) * 255.0f),
            255
        };
        voxelSet.voxels_[i].color_ = c;
    }
}

// Create a voxel-pool entity carved to match an SDF shape.  Allocates a
// centered box of the given halfExtent, then deactivates every voxel whose
// SDF value exceeds the 0.5 surface threshold.
EntityId createVoxelPoolShape(
    vec3 position, IRRender::ShapeType type, vec4 shapeParams, Color color, ivec3 halfExtent
) {
    ivec3 size = halfExtent * 2 + ivec3(1);
    EntityId entity =
        IREntity::createEntity(C_LocalTransform{position}, C_VoxelSetNew{size, color, true});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    auto sdfType = static_cast<IRMath::SDF::ShapeType>(type);
    vec4 sdfParams = IRMath::SDF::effectiveParams(sdfType, shapeParams);

    // Batch-evaluate the SDF over the whole grid via the shared helper rather
    // than calling evaluate() per cell. evaluateGrid samples each cell at
    // vec3(x,y,z) - size*0.5 + 0.5 in index3DtoIndex1D order — bit-identical to
    // the centered positions C_VoxelSetNew laid out for centerAroundOrigin — so
    // distances[i] lines up one-for-one with voxel i.
    std::vector<float> distances(static_cast<std::size_t>(vs.numVoxels_));
    IRMath::SDF::evaluateGrid(vs.size_, sdfType, sdfParams, distances);

    int activeCount = 0;
    vs.editVoxels([&](int i, C_Voxel &voxel, vec3) {
        if (distances[i] > IRMath::SDF::kSurfaceThreshold) {
            voxel.deactivate();
        } else {
            ++activeCount;
        }
    });
    // Debug tint is opt-in: --depth-color visualizes z-bands; --checkerboard
    // distinguishes adjacent same-color voxels. Default is plain shape color
    // — the checkerboard path was flickering frame-to-frame and breaking
    // visual regression tests.
    if (g_depthColor) {
        applyDepthColor(vs, type, sdfParams);
        // Set the scatter path's per-pixel depth-color mode so non-cardinal
        // yaw evaluates hue continuously in the fragment shader (#1697).
        vec3 bh = IRMath::SDF::boundingHalf(sdfType, sdfParams);
        IRRender::setDepthColorDebug(true, bh.x + bh.y + bh.z);
    } else if (g_checkerboard) {
        applyCheckerboard(vs, color);
    }

    IR_LOG_INFO(
        "VoxelPool shape entity={} canvas={} total={} active={}",
        entity,
        vs.canvasEntity_,
        vs.numVoxels_,
        activeCount
    );
    return entity;
}

// Directly-authored asymmetric voxel figure — the non-uniform stress case for
// the #1937 analytic per-axis-scatter edge coverage. Unlike the symmetric SDF
// primitives, it has appendages (a horizontal right arm, a RAISED diagonal
// staircase left arm, two legs in an offset stance) and slanted, non-axis-aligned
// surface planes (the staircase arm + a head visor), so under camera Z-yaw many
// differently-oriented silhouette edges face the camera at once — exactly where a
// corner-spike / dashing / seam regression would show. Centred at origin so
// --spin-shape's yaw-about-origin keeps it screen-locked. Opt-in (--spin-shape
// figure); never in the default scene, so committed references stay byte-identical.
EntityId createCustomVoxelFigure(vec3 position, Color color) {
    const ivec3 size{13, 9, 17}; // centred: x[-6,6] y[-4,4] z[-8,8] — demo native scale
    EntityId entity =
        IREntity::createEntity(C_LocalTransform{position}, C_VoxelSetNew{size, color, true});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    const auto box = [](int v, int lo, int hi) { return v >= lo && v <= hi; };
    const auto solid = [&](int x, int y, int z) -> bool {
        if (box(x, -2, 2) && box(y, -1, 1) && box(z, -3, 4))
            return true; // torso
        if (box(x, -1, 1) && box(y, -1, 1) && box(z, 5, 7))
            return true; // head (adjacent to torso top at z=4)
        if (box(x, 3, 5) && box(y, 0, 1) && box(z, 1, 2))
            return true;               // right arm (box appendage)
        for (int s = 0; s <= 3; ++s) { // left arm (raised staircase)
            if (x == -3 - s && box(y, -1, 0) && z == 3 + s)
                return true;
        }
        if (box(x, -2, -1) && box(y, -1, 1) && box(z, -8, -4))
            return true; // left leg
        if (box(x, 1, 2) && box(y, 0, 2) && box(z, -8, -4))
            return true;               // right leg (offset stance)
        for (int s = 0; s <= 1; ++s) { // head visor (slanted plane)
            if (box(x, -1, 1) && y == 2 + s && z == 6 - s)
                return true;
        }
        return false;
    };

    int activeCount = 0;
    vs.editVoxels([&](int, C_Voxel &voxel, vec3 p) {
        if (!solid(IRMath::round(p.x), IRMath::round(p.y), IRMath::round(p.z))) {
            voxel.deactivate();
        } else {
            ++activeCount;
        }
    });
    IR_LOG_INFO(
        "Custom voxel figure entity={} canvas={} size=({},{},{}) active={}",
        entity,
        vs.canvasEntity_,
        size.x,
        size.y,
        size.z,
        activeCount
    );
    return entity;
}

// Create an SDF shape entity at the given position.
EntityId createSDFShape(vec3 position, IRRender::ShapeType type, vec4 params, Color color) {
    C_ShapeDescriptor desc{type, params, color};
    // Same opt-in debug-tint toggle as createVoxelPoolShape, applied GPU-side
    // via shader flags. Default is no tint — the checkerboard path flickered
    // frame-to-frame on this side too.
    if (g_depthColor) {
        desc.flags_ |= IRRender::SHAPE_FLAG_DEPTH_COLOR;
    } else if (g_checkerboard) {
        desc.flags_ |= IRRender::SHAPE_FLAG_CHECKERBOARD;
    }
    EntityId entity = IREntity::createEntity(C_LocalTransform{position}, desc);
    auto &sd = IREntity::getComponent<C_ShapeDescriptor>(entity);
    IR_LOG_INFO(
        "SDF shape entity={} canvas={} type={} params=({},{},{},{})",
        entity,
        sd.canvasEntity_,
        static_cast<int>(type),
        params.x,
        params.y,
        params.z,
        params.w
    );
    return entity;
}

// Spawn one voxel cube routed through the GPU voxel-position prepass (#1396).
// The fixed 45° SO(3) rotation can only reach the rendered voxels via the
// prepass — UPDATE_VOXEL_SET_CHILDREN folds in translation only — so a rotated
// cube on screen is the smoke test that the prepass computed modelToWorld *
// localPos for every voxel in this set. Opt-in (--gpu-voxel-smoke) so the
// default scene stays byte-identical.
void createGpuVoxelTransformSmoke() {
    const vec3 axis = IRMath::normalize(vec3(0.3f, 0.7f, 0.5f));
    const vec4 rot = IRMath::quatAxisAngle(axis, IRMath::kQuarterPi);
    const vec3 position = vec3(0.0f, 0.0f, -14.0f);
    const ivec3 halfExtent = ivec3(5, 5, 5);
    const ivec3 size = halfExtent * 2 + ivec3(1);

    EntityId entity = IREntity::createEntity(
        C_LocalTransform{position, rot},
        C_VoxelSetNew{size, Color{120, 220, 160, 255}, true}
    );
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(entity);

    // Slot 0 carries this set's SO(3)+translation each frame; every owned voxel
    // points at slot 0 so the prepass transforms it instead of the CPU path.
    constexpr std::uint32_t kSmokeSlot = 0u;
    vs.gpuTransformSlot_ = kSmokeSlot;
    auto &pool = IREntity::getComponent<C_VoxelPool>(vs.canvasEntity_);
    pool.setTransformIndexForRange(
        vs.voxelStartIdx_,
        static_cast<size_t>(vs.numVoxels_),
        kSmokeSlot
    );
    IR_LOG_INFO(
        "GPU voxel-transform smoke entity={} canvas={} voxels={} slot={}",
        entity,
        vs.canvasEntity_,
        vs.numVoxels_,
        kSmokeSlot
    );
}

// Spawn one 2-bone rigged voxel bar skinned through the per-voxel bone→slot
// seed path (#1605). The bar spans x ∈ [-8, 8]; voxels left of the midpoint
// are painted bone 0 (root, at the left end), the rest bone 1 (elbow, at the
// midpoint, posed 30° off its bind rotation). UPDATE_JOINT_MATRICES allocates
// the skeleton's slot block on its first tick and auto-seeds binding 17 with
// `slotBase + bone_id`, so the right half visibly bends about the midpoint —
// a rigid entity transform cannot produce that, making the bend direct proof
// the per-voxel slots route through the joint skin matrices. Opt-in
// (--skin-smoke) so the default scene stays byte-identical.
void createSkinnedVoxelSmoke() {
    const vec3 position = vec3(0.0f, 16.0f, -14.0f);
    const ivec3 size = ivec3(17, 3, 3);

    EntityId rigRoot = IREntity::createEntity(
        C_LocalTransform{position},
        C_VoxelSetNew{size, Color{220, 140, 100, 255}, true}
    );
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(rigRoot);

    // Paint bone ids from each voxel's entity-local x: left half follows the
    // root bone, right half the elbow.
    for (std::size_t i = 0; i < vs.voxels_.size(); ++i) {
        vs.voxels_[i].bone_id_ = (vs.positions_[i].pos_.x < 0.0f) ? 0 : 1;
    }

    // Root joint at the bar's left end; elbow at the midpoint, CHILD_OF the
    // root with a +8 local offset and posed 30° about +Y away from its rest.
    // bindPose_ holds the rig-root-local REST chain (no pose rotation), so the
    // elbow's skin matrix is non-identity and bends the right half.
    EntityId rootJoint =
        IREntity::createEntity(C_Joint{}, C_LocalTransform{vec3(-8.0f, 0.0f, 0.0f)});
    IREntity::setParent(rootJoint, rigRoot);
    const vec4 elbowPose = IRMath::quatAxisAngle(vec3(0.0f, 1.0f, 0.0f), IRMath::kPi / 6.0f);
    EntityId elbowJoint =
        IREntity::createEntity(C_Joint{}, C_LocalTransform{vec3(8.0f, 0.0f, 0.0f), elbowPose});
    IREntity::setParent(elbowJoint, rootJoint);

    C_Skeleton skeleton;
    skeleton.joints_ = {rootJoint, elbowJoint};
    skeleton.bindPose_ = {
        IRMath::SQT{vec3(1.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(-8.0f, 0.0f, 0.0f)},
        IRMath::SQT{vec3(1.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(0.0f, 0.0f, 0.0f)},
    };
    IREntity::setComponent(rigRoot, skeleton);

    IR_LOG_INFO(
        "Skinned voxel smoke entity={} canvas={} voxels={} joints={}",
        rigRoot,
        vs.canvasEntity_,
        vs.numVoxels_,
        skeleton.joints_.size()
    );
}

// ---------------------------------------------------------------------------
// Shape definitions — each entry produces one voxel-pool entity and one
// SDF entity, placed side by side for visual comparison.
// ---------------------------------------------------------------------------

struct ShapeTestCase {
    const char *label_;
    IRRender::ShapeType type_;
    vec4 params_;
    ivec3 halfExtent_;
    Color color_;
};

// Minimal scene for --pivot-focus-demo (#1921): a tall, off-center pillar plus
// four distinct ground markers ringing it (one per world cardinal). The dedicated
// shot table pins the camera Z-yaw pivot on the pillar's true center, so under
// the fix the pillar holds dead-center while the markers visibly orbit it; with
// --pivot-origin the legacy z=0 pivot swings the whole group in an arc. Isolated
// from the cluttered default scene so the pin is unambiguous.
void initPivotFocusScene() {
    // Pillar at kPivotPillarCenter (z 0..20, off the world origin in x/y).
    createVoxelPoolShape(
        kPivotPillarCenter,
        IRRender::ShapeType::BOX,
        vec4(5, 5, 21, 0),
        Color{230, 200, 120, 255},
        ivec3(2, 2, 10)
    );
    // Asymmetric ground markers (z just above 0) so rotation is legible and its
    // direction unambiguous — distinct colors at +X / +Y / -X / -Y of the pillar.
    struct Marker {
        vec3 offset_;
        Color color_;
    };
    const Marker markers[] = {
        {vec3(14.0f, 0.0f, 0.0f), Color{220, 80, 80, 255}},
        {vec3(0.0f, 14.0f, 0.0f), Color{80, 120, 220, 255}},
        {vec3(-14.0f, 0.0f, 0.0f), Color{90, 200, 110, 255}},
        {vec3(0.0f, -14.0f, 0.0f), Color{210, 110, 210, 255}},
    };
    for (const auto &m : markers) {
        const vec3 pos = vec3(kPivotPillarCenter.x, kPivotPillarCenter.y, 1.0f) + m.offset_;
        createVoxelPoolShape(
            pos,
            IRRender::ShapeType::BOX,
            vec4(3, 3, 3, 0),
            m.color_,
            ivec3(1, 1, 1)
        );
    }
}

// Map a --spin-shape token to its ShapeType. Tokens match the lower-cased
// fixture shapes below; returns false (leaving `out` untouched) on an unknown
// name so the caller can diagnose it.
bool spinShapeTypeFromName(const std::string &name, IRRender::ShapeType &out) {
    struct SpinShapeName {
        const char *token_;
        IRRender::ShapeType type_;
    };
    static const SpinShapeName kSpinShapeNames[] = {
        {"box", IRRender::ShapeType::BOX},
        {"sphere", IRRender::ShapeType::SPHERE},
        {"cylinder", IRRender::ShapeType::CYLINDER},
        {"ellipsoid", IRRender::ShapeType::ELLIPSOID},
        {"cone", IRRender::ShapeType::CONE},
        {"torus", IRRender::ShapeType::TORUS},
        {"wedge", IRRender::ShapeType::WEDGE},
        {"curved_panel", IRRender::ShapeType::CURVED_PANEL},
    };
    for (const auto &entry : kSpinShapeNames) {
        if (name == entry.token_) {
            out = entry.type_;
            return true;
        }
    }
    return false;
}

// Canvas lighting setup shared by the full fixture scene and the --spin-shape
// single-shape scene: the main (voxel-pool) canvas needs the AO / sun-shadow /
// light-volume textures + C_TrixelCanvasRenderBehavior, or the lighting
// systems' archetype filter skips it and the shapes render unlit.
void setupCanvasLighting() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IR_LOG_INFO("Active canvas entity: {}", mainCanvas);

    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});

    // The voxel-pool canvas prefab doesn't include this component, so the
    // AO / sun-shadow / light-volume / lighting systems' archetype filter
    // wouldn't otherwise match the main canvas and they'd silently skip it.
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    // Default sun direction: high and slightly off-axis so every demo
    // shape casts a visible shadow without any further setup.
    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
}

void initEntities() {
    if (g_pivotFocusDemo) {
        IR_LOG_INFO("--- Camera pivot-focus demo scene (#1921) ---");
        initPivotFocusScene();
        return;
    }

    constexpr float kSpacingX = 16.0f;
    constexpr float kRowSeparationY = 12.0f;

    // params follow C_ShapeDescriptor conventions:
    //   BOX:          (sizeX, sizeY, sizeZ, 0)     — voxel counts per axis
    //   SPHERE:       (radius, radius, radius, 0)
    //   CYLINDER:     (radius, radius, height, 0)   — height = full height
    //   ELLIPSOID:    (2*rx, 2*ry, 2*rz, 0)        — diameters
    //   CONE:         (baseRadius, baseRadius, height, 0)
    //   TORUS:        (majorR, minorR, 0, 0)
    //   WEDGE:        (width, depth, height, 0)     — full sizes like BOX
    //   CURVED_PANEL: (width, depth, thickness, curvature)
    ShapeTestCase cases[] = {
        {"Box 7",
         IRRender::ShapeType::BOX,
         vec4(7, 7, 7, 0),
         ivec3(3, 3, 3),
         Color{100, 200, 220, 255}},

        {"Sphere r4",
         IRRender::ShapeType::SPHERE,
         vec4(4, 4, 4, 0),
         ivec3(5, 5, 5),
         Color{220, 180, 100, 255}},

        {"Cylinder",
         IRRender::ShapeType::CYLINDER,
         vec4(3, 3, 7, 0),
         ivec3(4, 4, 4),
         Color{100, 220, 140, 255}},

        {"Ellipsoid",
         IRRender::ShapeType::ELLIPSOID,
         vec4(8, 6, 4, 0),
         ivec3(5, 4, 3),
         Color{200, 130, 220, 255}},

        {"Cone",
         IRRender::ShapeType::CONE,
         vec4(4, 4, 8, 0),
         ivec3(5, 5, 4),
         Color{220, 140, 100, 255}},

        {"Torus",
         IRRender::ShapeType::TORUS,
         vec4(4, 2, 0, 0),
         ivec3(7, 7, 3),
         Color{100, 180, 220, 255}},

        {"Wedge",
         IRRender::ShapeType::WEDGE,
         vec4(7, 7, 7, 0),
         ivec3(4, 4, 4),
         Color{180, 220, 100, 255}},

        {"CurvedPanel",
         IRRender::ShapeType::CURVED_PANEL,
         vec4(8, 8, 2, 0.5f),
         ivec3(5, 5, 5),
         Color{220, 100, 180, 255}},
    };
    constexpr int kNumCases = sizeof(cases) / sizeof(cases[0]);

    // --spin-shape <name> (#1922): replace the side-by-side fixture scene with
    // ONE shape centred at the origin. Under camera Z-yaw-about-origin the shape
    // stays screen-centred, so the whole frame is that shape — clean per-shape
    // isolation for the temporal-jitter sweep. No floor / point light: a black
    // field maximises the metric's interior mask. The flag is absent in every
    // normal run, so the fixture scene below stays byte-identical.
    if (!g_spinShapeType.empty()) {
        // "figure" is the non-uniform stress case (not an SDF primitive): a
        // directly-authored asymmetric voxel set with appendages + slanted
        // planes, to verify the #1937 analytic edge coverage under camera yaw.
        if (g_spinShapeType == "figure") {
            createCustomVoxelFigure(vec3(0.0f, 0.0f, 0.0f), Color{210, 180, 140, 255});
            IR_LOG_INFO("Spin-shape single fixture: custom voxel figure");
            setupCanvasLighting();
            return;
        }
        IRRender::ShapeType want;
        if (spinShapeTypeFromName(g_spinShapeType, want)) {
            for (const auto &tc : cases) {
                if (tc.type_ != want) {
                    continue;
                }
                if (g_spinShapeVoxel) {
                    createVoxelPoolShape(
                        vec3(0.0f, 0.0f, 0.0f),
                        tc.type_,
                        tc.params_,
                        tc.color_,
                        tc.halfExtent_
                    );
                } else {
                    createSDFShape(vec3(0.0f, 0.0f, 0.0f), tc.type_, tc.params_, tc.color_);
                }
                IR_LOG_INFO(
                    "Spin-shape single fixture: {} ({})",
                    tc.label_,
                    g_spinShapeVoxel ? "voxel-pool" : "SDF"
                );
                break;
            }
        } else {
            IR_LOG_ERROR(
                "--spin-shape: unknown shape '{}' "
                "(box|sphere|cylinder|ellipsoid|cone|torus|wedge|curved_panel|figure)",
                g_spinShapeType
            );
        }
        setupCanvasLighting();
        return;
    }

    for (int i = 0; i < kNumCases; ++i) {
        auto &tc = cases[i];
        float xPos = i * kSpacingX;

        IR_LOG_INFO("--- {} ---", tc.label_);

        createVoxelPoolShape(
            vec3(xPos, 0.0f, 0.0f),
            tc.type_,
            tc.params_,
            tc.color_,
            tc.halfExtent_
        );

        createSDFShape(vec3(xPos, kRowSeparationY, 0.0f), tc.type_, tc.params_, tc.color_);
    }

    // LOD demonstration (#1467). The engine LOD filter is an inclusive band
    // [lodMax_ .. lodMin_], so co-located variants with DISJOINT bands render
    // exclusively — exactly one per zoom, swapping in place instead of
    // stacking. Three variants with deliberately distinct silhouettes
    // (cube -> cone -> sphere) share one world position; as the camera zooms
    // in the silhouette visibly changes, the unambiguous "LOD is working"
    // signal the issue asks for. A single-LOD control shape beside the stack
    // stays constant at every zoom for comparison.
    //
    // Bands vs. zoom (computeLodLevel: zoom<2 -> LOD_4, <4 -> LOD_3, <8 ->
    // LOD_2, <16 -> LOD_1, >=16 -> LOD_0):
    //   coarse cube   band [LOD_3 .. LOD_4]  -> zoom 1..3
    //   mid    cone   band [LOD_1 .. LOD_2]  -> zoom 4..15
    //   fine   sphere band [LOD_0 .. LOD_0]  -> zoom >=16 (persists at 32/64)
    // The coarsest variant keeps lodMin_ = LOD_4 (persists at min zoom); the
    // finest keeps lodMax_ = LOD_0 (persists past its threshold).
    constexpr float kLodFixtureY = -16.0f;
    constexpr float kLodControlX = 14.0f;
    struct LodVariant {
        IRRender::ShapeType type_;
        vec4 params_;
        IRRender::LodLevel lodMin_; // coarsest tier (largest index) visible
        IRRender::LodLevel lodMax_; // finest tier (smallest index) visible
        Color color_;
        const char *label_;
    };
    const LodVariant lodStack[] = {
        {IRRender::ShapeType::BOX,
         vec4(8, 8, 8, 0),
         IRRender::LodLevel::LOD_4,
         IRRender::LodLevel::LOD_3,
         Color{80, 130, 240, 255},
         "coarse cube (zoom 1-3)"},
        {IRRender::ShapeType::CONE,
         vec4(5, 5, 11, 0),
         IRRender::LodLevel::LOD_2,
         IRRender::LodLevel::LOD_1,
         Color{80, 240, 100, 255},
         "mid cone (zoom 4-15)"},
        {IRRender::ShapeType::SPHERE,
         vec4(4, 4, 4, 0),
         IRRender::LodLevel::LOD_0,
         IRRender::LodLevel::LOD_0,
         Color{240, 80, 80, 255},
         "fine sphere (zoom >=16)"},
    };
    for (const auto &v : lodStack) {
        IR_LOG_INFO("--- LOD variant: {} ---", v.label_);
        C_ShapeDescriptor desc{v.type_, v.params_, v.color_};
        desc.lodMin_ = v.lodMin_;
        desc.lodMax_ = v.lodMax_;
        IREntity::createEntity(C_LocalTransform{vec3(0.0f, kLodFixtureY, 0.0f)}, desc);
    }

    // Single-LOD control: default band (always visible), constant across the
    // whole zoom range so the swapping stack reads against a fixed reference.
    {
        IR_LOG_INFO("--- LOD control: constant cylinder (all zooms) ---");
        C_ShapeDescriptor control{
            IRRender::ShapeType::CYLINDER,
            vec4(4, 4, 9, 0),
            Color{230, 200, 90, 255}
        };
        IREntity::createEntity(C_LocalTransform{vec3(kLodControlX, kLodFixtureY, 0.0f)}, control);
    }

    // Rotation test: SDF shapes with non-identity entity rotation,
    // paired with unrotated copies for visual comparison.
    constexpr float kRotFixtureY = -32.0f;
    constexpr float kRotPairSpacing = 14.0f;
    struct RotFixture {
        const char *label_;
        IRRender::ShapeType type_;
        vec4 params_;
        vec3 axis_;
        float angleDeg_;
        Color color_;
    };
    const RotFixture rotFixtures[] = {
        {"Box 45° Z",
         IRRender::ShapeType::BOX,
         vec4(7, 7, 7, 0),
         vec3(0, 0, 1),
         45.0f,
         Color{100, 200, 220, 255}},
        {"Cylinder 30° Z",
         IRRender::ShapeType::CYLINDER,
         vec4(3, 3, 7, 0),
         vec3(0, 0, 1),
         30.0f,
         Color{100, 220, 140, 255}},
        {"Ellipsoid 45° Y",
         IRRender::ShapeType::ELLIPSOID,
         vec4(8, 6, 4, 0),
         vec3(0, 1, 0),
         45.0f,
         Color{200, 130, 220, 255}},
        {"Cone 60° X",
         IRRender::ShapeType::CONE,
         vec4(4, 4, 8, 0),
         vec3(1, 0, 0),
         60.0f,
         Color{220, 140, 100, 255}},
    };
    constexpr int kNumRotFixtures = sizeof(rotFixtures) / sizeof(rotFixtures[0]);
    for (int i = 0; i < kNumRotFixtures; ++i) {
        const auto &rf = rotFixtures[i];
        float xBase = i * (kRotPairSpacing * 2.0f);
        float angleRad = rf.angleDeg_ * IRMath::kPi / 180.0f;
        vec4 rot = IRMath::quatAxisAngle(rf.axis_, angleRad);

        createSDFShape(vec3(xBase, kRotFixtureY, 0.0f), rf.type_, rf.params_, rf.color_);

        C_ShapeDescriptor desc{rf.type_, rf.params_, rf.color_};
        if (g_depthColor)
            desc.flags_ |= IRRender::SHAPE_FLAG_DEPTH_COLOR;
        else if (g_checkerboard)
            desc.flags_ |= IRRender::SHAPE_FLAG_CHECKERBOARD;
        IREntity::createEntity(
            C_LocalTransform{vec3(xBase + kRotPairSpacing, kRotFixtureY, 0.0f), rot},
            desc
        );
    }

    // Floor so AO / sun-shadow lighting has a surface to fall on. +Z is
    // downward in this iso convention, so shape bottoms sit at max +z ≈ 4
    // (sphere r4, cone h8); floor sits just below at z ≈ 5.
    constexpr float kFloorZ = 5.0f;

    IR_LOG_INFO("--- Floor ---");
    EntityId floorEntity = createSDFShape(
        vec3((kNumCases - 1) * kSpacingX * 0.5f, kRowSeparationY * 0.5f, kFloorZ),
        IRRender::ShapeType::BOX,
        vec4(kNumCases * kSpacingX + 16.0f, kRowSeparationY + 24.0f, 2.0f, 0.0f),
        Color{150, 150, 160, 255}
    );
    IREntity::setComponent(floorEntity, C_LightBlocker{false, false, 0.0f});

    setupCanvasLighting();

    // Emissive point light placed between the shape rows so its colored
    // falloff is visible across both the voxel-pool and SDF copies of the
    // nearby shapes. Cyan reads cleanly against the warm shape palette.
    IREntity::createEntity(
        C_LocalTransform{vec3(40.0f, 6.0f, -2.0f)},
        C_LightSource{LightType::EMISSIVE, Color{80, 200, 255, 255}, 2.0f, static_cast<uint8_t>(30)}
    );

    // --load-vxs: load a DENSE-mode .vxs file (frame 0) and place the voxel
    // set at the origin so it can be compared against the procedural shapes.
    if (!g_loadVxsPath.empty()) {
        auto loaded = IRAsset::loadDenseVoxelSet(g_loadVxsPath);
        if (!loaded.ok()) {
            IR_LOG_ERROR("--load-vxs: could not load '{}'", g_loadVxsPath);
        } else if (loaded.value_.dense_.voxels_.size() != loaded.value_.dense_.voxelCount()) {
            IR_LOG_ERROR("--load-vxs: voxel count mismatch in '{}'", g_loadVxsPath);
        } else {
            auto voxelSet = IRPrefab::DenseVoxel::toComponent(loaded.value_.dense_);
            EntityId vxsEntity = IREntity::createEntity(
                C_LocalTransform{vec3(-20.0f, -8.0f, 0.0f)},
                std::move(voxelSet)
            );
            IR_LOG_INFO(
                "--load-vxs: loaded '{}' -> entity {} ({} voxels)",
                g_loadVxsPath,
                vxsEntity,
                loaded.value_.dense_.voxelCount()
            );
        }
    }

    if (g_gpuVoxelSmoke) {
        IR_LOG_INFO("--- GPU voxel-position prepass smoke (#1396) ---");
        createGpuVoxelTransformSmoke();
    }

    if (g_skinSmoke) {
        IR_LOG_INFO("--- Skinned voxel bone->slot smoke (#1605) ---");
        createSkinnedVoxelSmoke();
    }
}
