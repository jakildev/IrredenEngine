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
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Systems
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

// Prefab helpers
#include <irreden/render/entity_canvas.hpp>

// Command suites
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include <cstring>
#include <cstdlib>
#include <string>

// canvas_stress exercises the detached-canvas voxel path: many entities,
// each owning its own per-entity canvas + voxel pool, are composited over a
// main-canvas GRID grid by ENTITY_CANVAS_TO_FRAMEBUFFER. It is the first
// demo to spawn RotationMode::DETACHED entities — the permanent visual
// regression canary for detached canvases and inter-trixel rendering.

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

namespace {

struct CanvasStressSettings {
    int mainGridSize_ = 5;
    int detachedCount_ = 5;
    float initialZoom_ = 1.0f;
    float cameraYaw_ = 0.0f;
};

CanvasStressSettings g_settings{};
int g_autoWarmupFrames = 0;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), "zoom1_yaw0"},
    {2.0f, vec2(0, 0), "zoom2_yaw0"},
};

// One detached object: a per-entity canvas (textures + voxel pool), a voxel
// cube allocated into that pool, and a world entity carrying C_EntityCanvas
// + RotationMode::DETACHED that ENTITY_CANVAS_TO_FRAMEBUFFER composites.
// Unit quaternion for a rotation of `angle` radians about `axis`.
vec4 quatAxisAngle(vec3 axis, float angle) {
    const vec3 a = IRMath::normalize(axis);
    const float h = angle * 0.5f;
    const float s = IRMath::sin(h);
    return vec4(a.x * s, a.y * s, a.z * s, IRMath::cos(h));
}

void spawnDetachedVoxelObject(int index, vec3 worldPos, vec4 rotationQuat, Color color) {
    // A higher-resolution canvas + cube keeps the per-face SO(3) deformation's
    // forward-mapping gaps sub-pixel once the composite magnifies the canvas.
    constexpr ivec2 kCanvasSize{128, 128};
    constexpr ivec3 kPoolSize{16, 16, 16};
    constexpr ivec3 kCubeSize{10, 10, 10};

    C_EntityCanvas canvas = IRPrefab::EntityCanvas::createWithVoxelPool(
        "detached_canvas_" + std::to_string(index), kCanvasSize, kPoolSize
    );

    // The voxel cube lives inside the detached canvas's pool. Its position is
    // canvas-local (centered at the canvas origin), not the world position.
    IREntity::createEntity(
        C_Position3D{vec3(0.0f)},
        C_VoxelSetNew{kCubeSize, color, true, canvas.canvasEntity_}
    );

    // The world entity carries the canvas wrapper + DETACHED rotation mode.
    // PROPAGATE_CANVAS_ROTATION copies C_LocalTransform's full SO(3) quaternion
    // onto the canvas; VOXEL_TO_TRIXEL_STAGE_1 bakes it into the voxel emit
    // (T-295). The composite stage places the canvas axis-aligned.
    IREntity::createEntity(
        C_Position3D{worldPos},
        C_LocalTransform{vec3(0.0f), rotationQuat},
        C_RotationMode{RotationMode::DETACHED},
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
    if (gridSize.is<int>()) g_settings.mainGridSize_ = gridSize.as<int>();
    sol::object detachedCount = table["detached_count"];
    if (detachedCount.is<int>()) g_settings.detachedCount_ = detachedCount.as<int>();
    sol::object zoom = table["initial_zoom"];
    if (zoom.is<float>()) g_settings.initialZoom_ = zoom.as<float>();
}

void parseArgs(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--yaw") == 0 && i + 1 < argc) {
            g_settings.cameraYaw_ = static_cast<float>(std::atof(argv[i + 1]));
            ++i;
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
        {IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>(),
    };

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
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

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
                C_Position3D{pos},
                C_VoxelSetNew{ivec3(3, 3, 3), gridColor(x, y, n), true}
            );
        }
    }

    // Detached entities: a grid of per-entity canvases, each at a distinct
    // SO(3) rotation. The world spacing must exceed the detached canvas
    // footprint (composited at canvasSize / mainCanvasSize of the framebuffer)
    // or the canvases overlap — kDetachedSpacing is sized for the 64-trixel canvas.
    const int detached = IRMath::max(0, g_settings.detachedCount_);
    constexpr float kDetachedSpacing = 160.0f;
    const int cols = IRMath::max(1, static_cast<int>(IRMath::ceil(IRMath::sqrt(
        static_cast<float>(IRMath::max(detached, 1))
    ))));
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
    for (int i = 0; i < detached; ++i) {
        const int col = i % cols;
        const int row = i / cols;
        const vec3 worldPos{
            (static_cast<float>(col) - colCenter) * kDetachedSpacing,
            (static_cast<float>(row) - rowCenter) * kDetachedSpacing,
            0.0f
        };
        // Angle ramps across [0, 30°); axis cycles so adjacent canvases differ.
        // The per-face SO(3) deformation (T-295) skews each voxel face — clean
        // for moderate rotation; past ~40° the forward-mapped trixels gap and
        // the low-res detached canvas magnifies it into scanline stripes.
        const float angle = IRMath::kPi / 6.0f * static_cast<float>(i) /
                            static_cast<float>(IRMath::max(detached - 1, 1));
        const vec4 rotation = quatAxisAngle(kAxes[i % 4], angle);
        constexpr Color kDistinct[]{
            {230, 70, 70, 255},
            {70, 210, 90, 255},
            {80, 110, 230, 255},
            {230, 200, 60, 255},
            {210, 90, 220, 255},
            {70, 210, 210, 255},
        };
        const Color color = kDistinct[i % 6];
        spawnDetachedVoxelObject(i, worldPos, rotation, color);
    }

    IR_LOG_INFO(
        "canvas_stress: main grid {}x{} ({} cubes), {} detached canvases",
        n,
        n,
        n * n,
        detached
    );
}
