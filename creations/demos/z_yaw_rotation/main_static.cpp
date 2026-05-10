#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>

// SYSTEMS
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include "common.hpp"

namespace {

// 0.5 degrees per frame → full revolution in ~720 frames (~12 s at 60 fps)
constexpr float kYawDeltaPerFrame = 3.14159265f / 360.0f;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), "zoom1_yaw0"},
    {2.0f, vec2(0, 0), "zoom2_yaw0"},
};

int g_autoWarmupFrames = 0;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);
    IR_LOG_INFO("Starting creation: z_yaw_rotation/static");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // Drive the camera yaw each frame. Anchored on C_CameraYaw (the engine's
    // camera entity always has this component), so endTick fires exactly once.
    auto yawRotateSystem = IRSystem::createSystem<C_CameraYaw>(
        "AutoYawRotate",
        [](C_CameraYaw &) {},
        []() { IRPrefab::Camera::rotateYaw(kYawDeltaPerFrame); }
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        yawRotateSystem,
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
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
    // Four shapes in a ring: alternating SDF / voxel-pool, two of each.
    // Arranged at cardinal iso-world positions so all four are visible from the
    // default camera angle. Z-yaw rotation sweeps them around the center.
    constexpr float kRingRadius = 10.0f;

    // North-west: SDF sphere
    IREntity::createEntity(
        C_Position3D{vec3(-kRingRadius, 0.0f, 0.0f)},
        C_ShapeDescriptor{IRRender::ShapeType::SPHERE, vec4(4, 4, 4, 0), Color{220, 140, 80, 255}}
    );

    // North-east: voxel-pool cube
    {
        ivec3 half{3, 3, 3};
        ivec3 size = half * 2 + ivec3(1);
        IREntity::createEntity(
            C_Position3D{vec3(0.0f, -kRingRadius, 0.0f)},
            C_VoxelSetNew{size, Color{100, 200, 220, 255}, true}
        );
    }

    // South-east: SDF box
    IREntity::createEntity(
        C_Position3D{vec3(kRingRadius, 0.0f, 0.0f)},
        C_ShapeDescriptor{IRRender::ShapeType::BOX, vec4(6, 6, 6, 0), Color{100, 220, 140, 255}}
    );

    // South-west: voxel-pool sphere
    {
        constexpr float kSphereRadius = 4.0f;
        ivec3 half{static_cast<int>(kSphereRadius) + 1,
                   static_cast<int>(kSphereRadius) + 1,
                   static_cast<int>(kSphereRadius) + 1};
        ivec3 size = half * 2 + ivec3(1);
        EntityId e = IREntity::createEntity(
            C_Position3D{vec3(0.0f, kRingRadius, 0.0f)},
            C_VoxelSetNew{size, Color{180, 100, 220, 255}, true}
        );
        auto &vs = IREntity::getComponent<C_VoxelSetNew>(e);
        carveSphere(vs, kSphereRadius);
    }

    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
}
