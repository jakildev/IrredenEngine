#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

// Components
#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_auto_yaw_rotate.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_camera_scroll_zoom.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_auto_spin_local_transform.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Prefab helpers
#include <irreden/render/camera.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/entity_canvas.hpp>

// Command suites
#include <irreden/common/command_suite_capture.hpp>

#include <cstring>
#include <cstdlib>
#include <string>

// canvas_stress exercises the detached-canvas voxel path: many entities,
// each owning its own per-entity canvas + voxel pool, are composited over a
// main-canvas GRID grid by ENTITY_CANVAS_TO_FRAMEBUFFER. It is the first
// demo to spawn RotationMode::DETACHED entities — the permanent visual
// regression canary for detached canvases and inter-trixel rendering.
//
// Per #1259 the demo also serves as the rotation-visualization showcase:
// camera yaw auto-rotates by default, DETACHED canvases spin continuously
// at per-entity rates around their assigned axes (full SO(3) bake), a
// small cluster of GRID-mode cubes re-rasterizes via REBUILD_GRID_VOXELS
// each frame, and the standard lighting pipeline (AO + sun + shadows)
// runs so face orientation is visually perceptible.

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

struct CanvasStressSettings {
    int mainGridSize_ = 5;
    int detachedCount_ = 5;
    float initialZoom_ = 1.0f;
    float cameraYaw_ = 0.0f;
    bool autoRotate_ = true;
    bool fullRotate_ = false;
    bool noSpin_ = false;
    bool noLighting_ = false;
};

// 0.5 degrees per frame → full revolution in ~720 frames (~12 s at 60 fps)
constexpr float kYawDeltaPerFrame = IRMath::kPi / 360.0f;

// SO(3) camera-rotate canary. Per-frame rates are chosen so the three axes
// never re-align at the same phase inside a normal capture window — GRID
// canvases stay axis-aligned (only Z reaches them via the yaw helpers)
// while DETACHED canvases tilt continuously, exercising the per-canvas
// SO(3) bake.
constexpr float kFullRotateYawPerFrame = IRMath::kPi / 540.0f; // ~3 rev / 1080 fr
constexpr float kFullRotatePitchPerFrame = IRMath::kPi / 720.0f;
constexpr float kFullRotatePitchYPerFrame =
    IRMath::kPi / 900.0f; // Y-axis; Z=yaw, X=pitch in ISO frame

// Per-entity continuous spin for DETACHED canvases (#1259). 0.4° / frame
// base rate scales by (i + 1) so adjacent canvases visibly de-sync within
// the auto-screenshot capture window. The slowest entity completes a full
// rotation in ~900 frames (~15 s at 60 fps); the fastest is ~5×.
constexpr float kDetachedSpinBaseRadPerFrame = IRMath::kPi / 450.0f;

// GRID-mode spin rate — slower than DETACHED so per-cell re-rasterization
// aliasing (cells aliased when adjacent voxels round to the same world
// cell after rotation) is visible as smooth swap-and-settle rather than
// a strobe. ~0.25° / frame → full revolution in ~1440 frames (~24 s).
constexpr float kGridSpinRadPerFrame = IRMath::kPi / 720.0f;

// Sun / lighting tuned for the canvas_stress layout — flat ground plane
// of GRID cubes plus floating DETACHED canvases. Slightly steeper than
// the standard lighting demo so detached cube tops and main-grid tops
// both get lambert above ambient at neutral camera yaw.
constexpr vec3 kSunDirection = vec3(-0.35f, -0.25f, -0.90f);
constexpr float kSunIntensity = 1.0f;
constexpr float kSunAmbient = 0.45f;

CanvasStressSettings g_settings{};
int g_autoWarmupFrames = 0;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "overview"},
    {0.6f, vec2(0, 0), 0.0f, "wide"},
};

// One detached object: a per-entity canvas (textures + voxel pool), a voxel
// cube allocated into that pool, and a world entity carrying C_EntityCanvas
// + RotationMode::DETACHED that ENTITY_CANVAS_TO_FRAMEBUFFER composites.

void spawnDetachedVoxelObject(
    int index, vec3 worldPos, vec3 spinAxis, float spinRate, Color color
) {
    // A higher-resolution canvas + cube keeps the per-face SO(3) deformation's
    // forward-mapping gaps sub-pixel once the composite magnifies the canvas.
    constexpr ivec2 kCanvasSize{128, 128};
    constexpr ivec3 kPoolSize{16, 16, 16};
    constexpr ivec3 kCubeSize{10, 10, 10};

    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        "detached_canvas_" + std::to_string(index),
        kCanvasSize,
        kPoolSize
    );

    // The voxel cube lives inside the detached canvas's pool. Its position is
    // canvas-local (centered at the canvas origin), not the world position.
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{kCubeSize, color, true, canvas.canvasEntity_}
    );

    // The world entity carries the canvas wrapper + DETACHED rotation mode +
    // continuous spin. PROPAGATE_CANVAS_ROTATION copies C_LocalTransform's
    // SO(3) quaternion onto the canvas; VOXEL_TO_TRIXEL_STAGE_1 bakes it
    // into the voxel emit (T-295). AUTO_SPIN_LOCAL_TRANSFORM advances the
    // quaternion each UPDATE tick before PROPAGATE_TRANSFORM runs.
    IREntity::createEntity(
        C_LocalTransform{worldPos},
        C_RotationMode{RotationMode::DETACHED},
        C_AutoSpin{spinAxis, spinRate},
        canvas
    );
}

Color gridColor(int x, int y, int gridSize) {
    const float denom = static_cast<float>(IRMath::max(gridSize - 1, 1));
    return Color{
        static_cast<std::uint8_t>(70 + 150.0f * (static_cast<float>(x) / denom)),
        static_cast<std::uint8_t>(110 + 110.0f * (static_cast<float>(y) / denom)),
        static_cast<std::uint8_t>(170),
        255
    };
}

void readConfig() {
    IRScript::LuaScript configScript{IREngine::resolveScriptPath("config.lua").c_str()};
    sol::table table = configScript.getTable("canvas_stress");
    if (!table.valid()) {
        return;
    }
    sol::object gridSize = table["main_grid_size"];
    if (gridSize.is<int>())
        g_settings.mainGridSize_ = gridSize.as<int>();
    sol::object detachedCount = table["detached_count"];
    if (detachedCount.is<int>())
        g_settings.detachedCount_ = detachedCount.as<int>();
    sol::object zoom = table["initial_zoom"];
    if (zoom.is<float>())
        g_settings.initialZoom_ = zoom.as<float>();
    sol::object autoRotate = table["auto_rotate"];
    if (autoRotate.is<bool>())
        g_settings.autoRotate_ = autoRotate.as<bool>();
}

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            g_settings.cameraYaw_ = static_cast<float>(std::atof(argv[i + 1]));
            ++i;
        } else if (std::strcmp(argv[i], "--auto-rotate") == 0) {
            g_settings.autoRotate_ = true;
        } else if (std::strcmp(argv[i], "--no-auto-rotate") == 0) {
            g_settings.autoRotate_ = false;
        } else if (std::strcmp(argv[i], "--full-rotate") == 0) {
            g_settings.fullRotate_ = true;
        } else if (std::strcmp(argv[i], "--no-spin") == 0) {
            g_settings.noSpin_ = true;
        } else if (std::strcmp(argv[i], "--no-lighting") == 0) {
            g_settings.noLighting_ = true;
        }
    }
}

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    parseArgs(argc, argv);
    IR_LOG_INFO("Starting creation: canvas_stress");
    IREngine::init(argv[0]);
    readConfig();

    initSystems();
    initCommands();
    initEntities();

    IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
    IRRender::setCameraZoom(g_settings.initialZoom_);
    IRPrefab::Camera::setYaw(g_settings.cameraYaw_);

    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::AUTO_SPIN_LOCAL_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();

    if (g_settings.autoRotate_) {
        renderPipeline.push_back(
            IRSystem::createSystem<IRSystem::AUTO_YAW_ROTATE>(kYawDeltaPerFrame)
        );
    }

    if (g_settings.fullRotate_) {
        // SO(3) camera driver — composes per-frame X/Y/Z spins into the
        // camera's C_LocalTransform.rotation_ via setRotationQuat. C_Camera
        // anchors the singleton tick; GRID canvases see only the Z-component
        // (via IRPrefab::Camera::getYaw); DETACHED canvases pick up the full
        // quat through PROPAGATE_CANVAS_ROTATION.
        renderPipeline.push_back(
            IRSystem::createSystem<C_Camera>(
                "AutoFullRotate",
                [](C_Camera &) {},
                []() {
                    const vec4 delta = IRMath::quatMul(
                        IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), kFullRotateYawPerFrame),
                        IRMath::quatMul(
                            IRMath::quatAxisAngle(
                                vec3(0.0f, 1.0f, 0.0f),
                                kFullRotatePitchYPerFrame
                            ),
                            IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), kFullRotatePitchPerFrame)
                        )
                    );
                    IRPrefab::Camera::setRotationQuat(
                        IRMath::quatMul(delta, IRPrefab::Camera::getRotationQuat())
                    );
                }
            )
        );
    }

    renderPipeline.push_back(IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>());
    renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());

    if (g_autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = g_autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    // Lighting wiring (#1259). The lighting pipeline writes per-canvas
    // shadow / AO / light-volume textures sized to the main canvas;
    // they're allocated once at startup. Sun direction / intensity /
    // ambient are global render state set below.
    if (!g_settings.noLighting_) {
        const ivec2 mainCanvasSize =
            IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
        IREntity::setComponent(mainCanvas, C_CanvasAOTexture{mainCanvasSize});
        IREntity::setComponent(mainCanvas, C_CanvasSunShadow{mainCanvasSize});
        IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
        IRRender::setSunDirection(kSunDirection);
        IRRender::setSunIntensity(kSunIntensity);
        IRRender::setSunAmbient(kSunAmbient);
        IRRender::setSunShadowsEnabled(true);
        IRRender::setAOEnabled(true);
    }

    // Main-canvas GRID grid: a flat lattice of small voxel cubes. Exercises
    // T-293 inter-trixel deformation on the world canvas under camera yaw.
    const int n = IRMath::max(0, g_settings.mainGridSize_);
    constexpr float kGridSpacing = 7.0f;
    const float gridCenter = (static_cast<float>(IRMath::max(n, 1)) - 1.0f) * 0.5f;
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            const vec3 pos{
                (static_cast<float>(x) - gridCenter) * kGridSpacing,
                (static_cast<float>(y) - gridCenter) * kGridSpacing,
                0.0f
            };
            IREntity::createEntity(
                C_LocalTransform{pos},
                C_VoxelSetNew{ivec3(3, 3, 3), gridColor(x, y, n), true}
            );
        }
    }

    // GRID-mode rotating cluster: a row of mid-sized cubes that spin in
    // place around staggered axes. Each tick AUTO_SPIN_LOCAL_TRANSFORM
    // advances C_LocalTransform.rotation_, PROPAGATE_TRANSFORM composes
    // C_WorldTransform, and REBUILD_GRID_VOXELS re-rasterizes the voxel
    // cells into the world pool (#1259 §C6 — face-swapping / cell-aliasing
    // path). Sits above the main grid so the re-rasterized cells are
    // clearly visible against the flat ground.
    constexpr int kGridSpinCount = 4;
    constexpr ivec3 kGridSpinCubeSize{5, 5, 5};
    constexpr float kGridSpinSpacing = 16.0f;
    constexpr vec3 kGridSpinAxes[kGridSpinCount]{
        {0.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
    };
    constexpr Color kGridSpinColors[kGridSpinCount]{
        {255, 120, 120, 255},
        {120, 255, 120, 255},
        {120, 160, 255, 255},
        {255, 220, 120, 255},
    };
    const float gridSpinCenter = (static_cast<float>(kGridSpinCount) - 1.0f) * 0.5f;
    const float gridSpinY = (static_cast<float>(n) * 0.5f + 1.5f) * kGridSpacing;
    for (int i = 0; i < kGridSpinCount; ++i) {
        const vec3 pos{
            (static_cast<float>(i) - gridSpinCenter) * kGridSpinSpacing,
            gridSpinY,
            -static_cast<float>(kGridSpinCubeSize.z) * 0.5f
        };
        IREntity::createEntity(
            C_LocalTransform{pos},
            C_RotationMode{RotationMode::GRID},
            C_AutoSpin{kGridSpinAxes[i], g_settings.noSpin_ ? 0.0f : kGridSpinRadPerFrame},
            C_VoxelSetNew{kGridSpinCubeSize, kGridSpinColors[i], true}
        );
    }

    // Detached entities: a grid of per-entity canvases, each at a distinct
    // SO(3) rotation. The world spacing must exceed the detached canvas
    // footprint (composited at canvasSize / mainCanvasSize of the framebuffer)
    // or the canvases overlap — kDetachedSpacing is sized for the 64-trixel canvas.
    const int detached = IRMath::max(0, g_settings.detachedCount_);
    constexpr float kDetachedSpacing = 160.0f;
    const int cols = IRMath::max(
        1,
        static_cast<int>(IRMath::ceil(IRMath::sqrt(static_cast<float>(IRMath::max(detached, 1)))))
    );
    const int rows = (detached + cols - 1) / IRMath::max(cols, 1);
    const float colCenter = (static_cast<float>(cols) - 1.0f) * 0.5f;
    const float rowCenter = (static_cast<float>(IRMath::max(rows, 1)) - 1.0f) * 0.5f;
    // Rotation axes cycled per entity so the grid shows yaw, pitch, roll, and
    // a mixed diagonal — full SO(3) baked into each canvas by T-295.
    constexpr vec3 kAxes[]{
        {0.0f, 0.0f, 1.0f}, // yaw
        {1.0f, 0.0f, 0.0f}, // pitch
        {0.0f, 1.0f, 0.0f}, // roll
        {1.0f, 1.0f, 1.0f}, // mixed diagonal
    };
    constexpr Color kDetachedColors[]{
        {230, 70, 70, 255},
        {70, 210, 90, 255},
        {80, 110, 230, 255},
        {230, 200, 60, 255},
        {210, 90, 220, 255},
        {70, 210, 210, 255},
    };
    for (int i = 0; i < detached; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const vec3 worldPos{
            (static_cast<float>(col) - colCenter) * kDetachedSpacing,
            (static_cast<float>(row) - rowCenter) * kDetachedSpacing,
            0.0f
        };
        // Per-entity spin rate: base * (i + 1) so adjacent canvases visibly
        // de-sync within the auto-screenshot capture window (#1259). Spin
        // axis cycles independently of rate so axis/rate pairs cover the
        // full SO(3) bake matrix.
        const float spinRate =
            g_settings.noSpin_ ? 0.0f : kDetachedSpinBaseRadPerFrame * static_cast<float>(i + 1);
        spawnDetachedVoxelObject(i, worldPos, kAxes[i % 4], spinRate, kDetachedColors[i % 6]);
    }

    IR_LOG_INFO(
        "canvas_stress: main grid {}x{} ({} cubes), {} GRID-spin cubes, {} detached canvases",
        n,
        n,
        n * n,
        kGridSpinCount,
        detached
    );
}
