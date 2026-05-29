// T3 / T4 composite verification vehicle — voxel-set-only Z-yaw demo.
//
// Purpose: validates the per-axis trixel→framebuffer depth composite (#1310 / T3)
// and AO/lighting on the resolved composite (#1311 / T4) in isolation from SDF shapes.
// See docs/design/per-axis-trixel-canvas-rotation.md and issue #1344 / epic #1307.
//
// Scene: varied voxel topology spread across the whole viewport so off-center geometry
// exercises the cull-in-yawed-space path (off-center objects move most under residual
// yaw; a cardinal-snapped cull drops them — the "most objects missing" symptom).
//
// Pre-T3: renders with 90°-snapping rotation (cardinal fast path only).
// Post-T3: shows smooth continuous yaw on the per-axis composite path.
//
// Usage:
//   fleet-run IRVoxelYaw --spin-yaw --auto-screenshot 24   # regression sweep
//   fleet-run IRVoxelYaw --yaw 0.524                       # park at ~30°

#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

// COMPONENTS
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

// SYSTEMS
#include <irreden/render/systems/system_lod_update.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_auto_yaw_rotate.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/camera_controls.hpp>

namespace {

// ROI crop tables are framebuffer-pixel coords; values below assume the
// 1280×720 default game resolution (`kGameResolution`). On HiDPI hosts the
// framebuffer is a power-of-two larger and the crops land in the upper-left
// quadrant of the captured image — still useful for edge-fidelity inspection
// at a given fixed offset. Pixel-precise crop placement is a per-host
// iteration point; refine the coords after the first visual run.
constexpr IRVideo::RoiCrop kCropsCubeSilhouette[] = {
    {580, 290, 96, 96, "cube_top_silhouette"},
};

constexpr IRVideo::RoiCrop kCropsStaircaseSeam[] = {
    {700, 360, 100, 80, "staircase_depth_seam"},
};

// Shot table:
//   - Cardinal shots FIRST: per-axis canvases (allocated at non-cardinal yaw) are
//     never freed mid-sequence. The non-cardinal→cardinal deallocation path has a
//     known crash in the current T2/T3 codebase (#1310 nit #2); the full four-
//     cardinal rebracket check is covered by the --spin-yaw sweep instead.
//   - --spin-yaw + --auto-screenshot: one full rotation sweep (takes priority).
constexpr IRVideo::AutoScreenshotShot kShots[] = {
    // Cardinal fast-path baseline (no per-axis canvases allocated yet)
    {1.0f, vec2(0, 0), 0.0f, "zoom1_yaw0"},
    {2.0f, vec2(0, 0), 0.0f, "zoom2_yaw0"},
    {4.0f,
     vec2(0, 0),
     0.0f,
     "zoom4_yaw0",
     kCropsCubeSilhouette,
     sizeof(kCropsCubeSilhouette) / sizeof(kCropsCubeSilhouette[0])},
    {8.0f,
     vec2(0, 0),
     0.0f,
     "zoom8_yaw0",
     kCropsStaircaseSeam,
     sizeof(kCropsStaircaseSeam) / sizeof(kCropsStaircaseSeam[0])},

    // Non-cardinal parked last: T3 steady-state composite (residualYaw != 0 every
    // frame). Per-axis canvases stay allocated until shutdown — avoids triggering
    // the non-cardinal→cardinal deallocation crash in the current codebase.
    {4.0f,
     vec2(0, 0),
     IRMath::kPi / 6.0f,
     "zoom4_yaw30",
     kCropsCubeSilhouette,
     sizeof(kCropsCubeSilhouette) / sizeof(kCropsCubeSilhouette[0])},
    {8.0f,
     vec2(0, 0),
     IRMath::kPi / 6.0f,
     "zoom8_yaw30_stair",
     kCropsStaircaseSeam,
     sizeof(kCropsStaircaseSeam) / sizeof(kCropsStaircaseSeam[0])},
    {4.0f, vec2(0, 0), IRMath::kQuarterPi, "zoom4_yaw45"},
};

int g_autoWarmupFrames = 0;
float g_spinYawDegPerSec = 0.0f;
int g_spinYawShotCount = 24;
float g_initialZoom = 0.0f;
float g_initialYawRadians = 0.0f;
bool g_initialYawSet = false;

// Dynamic shot table for --spin-yaw + --auto-screenshot sweep.
// Pointer stability required: reserve before push_back.
std::vector<IRVideo::AutoScreenshotShot> g_spinYawShots;
std::vector<std::array<char, 32>> g_spinYawShotLabels;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--spin-yaw") == 0) {
            g_spinYawDegPerSec = 30.0f;
            if (i + 1 < argc) {
                float v = static_cast<float>(std::atof(argv[i + 1]));
                if (v > 0.0f) {
                    g_spinYawDegPerSec = v;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--zoom") == 0) {
            if (i + 1 < argc) {
                float z = static_cast<float>(std::atof(argv[i + 1]));
                if (z > 0.0f) {
                    g_initialZoom = z;
                    ++i;
                }
            }
        } else if (std::strcmp(argv[i], "--yaw") == 0) {
            if (i + 1 < argc) {
                g_initialYawRadians = static_cast<float>(std::atof(argv[i + 1]));
                g_initialYawSet = true;
                ++i;
            }
        }
    }

    // --spin-yaw + --auto-screenshot: reinterpret the screenshot value as
    // "shots across one rotation" with a small internal warmup.
    if (g_spinYawDegPerSec > 0.0f && g_autoWarmupFrames > 0) {
        g_spinYawShotCount = g_autoWarmupFrames;
        g_autoWarmupFrames = 10;
        IR_LOG_INFO(
            "Spin-yaw: warmup reset to 10 frames (--auto-screenshot value {} reinterpreted as shot count)",
            g_spinYawShotCount
        );
    }

    IR_LOG_INFO("Starting creation: ir_voxel_yaw");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    if (g_initialZoom > 0.0f)
        IRRender::setCameraZoom(g_initialZoom);
    if (g_initialYawSet) {
        IRPrefab::Camera::setYaw(g_initialYawRadians);
        IR_LOG_INFO("Initial camera Z-yaw: {} rad", g_initialYawRadians);
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
    // --spin-yaw live mode: drive the camera each frame.
    // In auto-screenshot mode per-shot setYaw() drives rotation instead — running
    // both would double-rotate between shots and break the evenly-spaced contract.
    if (g_spinYawDegPerSec > 0.0f && g_autoWarmupFrames == 0) {
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
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        if (g_spinYawDegPerSec > 0.0f) {
            // Sweep one full rotation. Default zoom=4 matches the shot table;
            // pass --zoom to sweep at high zoom (where parity glitches are visible
            // at full pixel scale). Reserve up front so push_back never reallocates
            // (a reallocation would invalidate label pointers already in g_spinYawShots).
            const float sweepZoom = g_initialZoom > 0.0f ? g_initialZoom : 4.0f;
            const int n = IRMath::max(2, g_spinYawShotCount);
            g_spinYawShotLabels.reserve(n);
            g_spinYawShots.reserve(n);
            for (int i = 0; i < n; ++i) {
                const float yaw =
                    (static_cast<float>(i) / static_cast<float>(n)) * IRMath::kTwoPi;
                auto &label = g_spinYawShotLabels.emplace_back();
                std::snprintf(label.data(), label.size(), "spin_yaw_%03d_of_%03d", i, n);
                g_spinYawShots.push_back({sweepZoom, vec2(0, 0), yaw, label.data()});
            }
            cfg.shots_ = g_spinYawShots.data();
            cfg.numShots_ = static_cast<int>(g_spinYawShots.size());
            IR_LOG_INFO(
                "Spin-yaw sweep: {} shots across one rotation at zoom={}",
                cfg.numShots_,
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
}

// Create a solid voxel box centered at `position`.
static EntityId createSolidBox(vec3 position, ivec3 halfExtent, Color color) {
    ivec3 size = halfExtent * 2 + ivec3(1);
    return IREntity::createEntity(C_LocalTransform{position}, C_VoxelSetNew{size, color, true});
}

// Create a hollow voxel shell (1-voxel-thick walls, interior carved).
// Validates exposed-face-mask correctness: interior faces become visible.
static EntityId createHollowShell(vec3 position, ivec3 halfExtent, Color color) {
    ivec3 size = halfExtent * 2 + ivec3(1);
    EntityId e = IREntity::createEntity(C_LocalTransform{position}, C_VoxelSetNew{size, color, true});
    auto &vs = IREntity::getComponent<C_VoxelSetNew>(e);
    for (int i = 0; i < vs.numVoxels_; ++i) {
        // Positions are stored as exact integer-valued floats relative to the center.
        ivec3 p(
            static_cast<int>(vs.positions_[i].pos_.x),
            static_cast<int>(vs.positions_[i].pos_.y),
            static_cast<int>(vs.positions_[i].pos_.z)
        );
        bool onSurface =
            IRMath::abs(p.x) >= halfExtent.x ||
            IRMath::abs(p.y) >= halfExtent.y ||
            IRMath::abs(p.z) >= halfExtent.z;
        if (!onSurface)
            vs.voxels_[i].deactivate();
    }
    vs.syncActiveMask();
    return e;
}

void initEntities() {
    // Scene: varied voxel topology spread to all four screen quadrants.
    // This is the T3/T4 composite verification vehicle — geometry placement
    // is deliberate: off-center objects exercise the cull-in-yawed-space
    // path, which must compute the cull from pos3DtoPos2DIsoYawed (not
    // cardinal-snapped positions) or off-center objects disappear at
    // oblique camera angles.

    // CENTER: solid cube 7×7×7 — silhouette baseline for ROI crops.
    createSolidBox(vec3(0.0f, 0.0f, 0.0f), ivec3(3, 3, 3), Color{100, 200, 220, 255});

    // RIGHT: hollow shell 9×9×9 (1-voxel walls) — exposed-face-mask correctness.
    createHollowShell(vec3(20.0f, 0.0f, 0.0f), ivec3(4, 4, 4), Color{220, 180, 100, 255});

    // FRONT-LEFT: lone voxel 1×1×1 — degenerate topology edge case.
    createSolidBox(vec3(-15.0f, -12.0f, 0.0f), ivec3(0, 0, 0), Color{220, 80, 80, 255});

    // BACK: tall wall 3×3×13 — depth parallax under yaw.
    // z = -6 positions the center 6 units above the ground plane (z+ = down);
    // the wall extends from z=-12 to z=0, rising above the other objects.
    createSolidBox(vec3(-5.0f, 14.0f, -6.0f), ivec3(1, 1, 6), Color{100, 220, 140, 255});

    // FRONT-RIGHT: staircase — 4 steps with depth cliffs, each 3×3×3.
    // Steps ascend in the x and -z direction to create cross-canvas seam targets.
    {
        const Color kStairColor{200, 130, 220, 255};
        for (int s = 0; s < 4; ++s) {
            vec3 stepPos = vec3(
                12.0f + static_cast<float>(s) * 4.0f,
                -10.0f,
                -static_cast<float>(s) * 3.0f
            );
            createSolidBox(stepPos, ivec3(1, 1, 1), kStairColor);
        }
    }

    // CORNER CLUSTERS: one 3×3×3 cube per screen quadrant.
    // At zoom=2–4 these land near the screen edges to catch cull regressions.
    createSolidBox(vec3( 25.0f, -18.0f, 0.0f), ivec3(1, 1, 1), Color{240, 200,  80, 255}); // front-right
    createSolidBox(vec3(-22.0f, -16.0f, 0.0f), ivec3(1, 1, 1), Color{ 80, 200, 240, 255}); // front-left
    createSolidBox(vec3( 22.0f,  18.0f, 0.0f), ivec3(1, 1, 1), Color{240,  80, 200, 255}); // back-right
    createSolidBox(vec3(-20.0f,  20.0f, 0.0f), ivec3(1, 1, 1), Color{200, 240,  80, 255}); // back-left

    // Required: activate the trixel-canvas render behavior on the main canvas
    // so VOXEL_TO_TRIXEL_STAGE_1 / TRIXEL_TO_FRAMEBUFFER archetype filters match.
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
}
