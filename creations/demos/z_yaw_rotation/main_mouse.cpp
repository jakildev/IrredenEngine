#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/camera.hpp>

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_light_source.hpp>

// SYSTEMS
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_build_light_occlusion_grid.hpp>
#include <irreden/render/systems/system_compute_voxel_ao.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_sun_shadow.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_lighting_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_screen_residual_rotate.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>

// COMMAND SUITES
#include <irreden/common/command_suite_camera.hpp>
#include <irreden/common/command_suite_capture.hpp>

#include "common.hpp"

using namespace IRComponents;

namespace {

// Radians per screen pixel of mouse X movement.
constexpr float kMouseRotationSensitivity = 0.004f;

// Frames the sun-flash effect lasts on entity click.
constexpr int kClickFlashDuration = 45;

// Sun directions: normal and during click flash.
constexpr vec3 kSunDirNormal{0.35f, 0.85f, -0.4f};
constexpr vec3 kSunDirFlash{-0.6f, 0.2f, -0.8f};

bool g_rotationActive = false;
float g_prevMouseX = 0.0f;
bool g_firstMouseFrame = true;
int g_clickFlashFrames = 0;

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: z_yaw_rotation/interactive");
    IR_LOG_INFO("  R — toggle mouse-driven yaw rotation");
    IR_LOG_INFO("  Left click on entity — sun-flash effect (proves per-voxel pick)");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IRRender::setHoveredTrixelVisible(true);
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    // Mouse-driven yaw + click detection.  Anchored on C_CameraYaw so
    // endTick fires exactly once per frame regardless of entity count.
    auto mouseYawSystem = IRSystem::createSystem<C_CameraYaw>(
        "MouseYawRotate",
        [](C_CameraYaw &) {},
        []() {
            // R toggles rotation
            if (IRInput::checkKeyMouseButton(IRInput::kKeyButtonR, IRInput::PRESSED)) {
                g_rotationActive = !g_rotationActive;
                g_firstMouseFrame = true;
                IR_LOG_INFO("Z-yaw mouse rotation: {}", g_rotationActive ? "ON" : "OFF");
            }

            vec2 mouseScreen = IRInput::getMousePositionScreen();

            if (!g_firstMouseFrame && g_rotationActive) {
                float deltaX = mouseScreen.x - g_prevMouseX;
                IRPrefab::Camera::rotateYaw(deltaX * kMouseRotationSensitivity);
            }
            g_prevMouseX = mouseScreen.x;
            g_firstMouseFrame = false;

            // Per-voxel entity pick + left-click flash effect
            IREntity::EntityId hovered = IRRender::getEntityIdAtMouseTrixel();
            bool leftClick =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
            if (leftClick && hovered != IREntity::kNullEntity) {
                g_clickFlashFrames = kClickFlashDuration;
                IR_LOG_INFO("Entity {} clicked (per-voxel pick confirmed)", hovered);
            }

            if (g_clickFlashFrames > 0) {
                --g_clickFlashFrames;
                IRRender::setSunDirection(kSunDirFlash);
            } else {
                IRRender::setSunDirection(kSunDirNormal);
            }
        }
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        mouseYawSystem,
        IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
        IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
        IRSystem::createSystem<IRSystem::BUILD_LIGHT_OCCLUSION_GRID>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
        IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
        IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::COMPUTE_VOXEL_AO>(),
        IRSystem::createSystem<IRSystem::BAKE_SUN_SHADOW_MAP>(),
        IRSystem::createSystem<IRSystem::COMPUTE_SUN_SHADOW>(),
        IRSystem::createSystem<IRSystem::COMPUTE_LIGHT_VOLUME>(),
        IRSystem::createSystem<IRSystem::LIGHTING_TO_TRIXEL>(),
        IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
        IRSystem::createSystem<IRSystem::SCREEN_SPACE_RESIDUAL_ROTATE>(),
    };

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initCommands() {
    IRCommand::registerCameraCommands();
    IRCommand::registerCaptureCommands();
}

void initEntities() {
    constexpr float kRingRadius = 10.0f;

    // North-west: SDF sphere (clickable — entity-id readback targets SDF canvases)
    IREntity::createEntity(
        C_Position3D{vec3(-kRingRadius, 0.0f, 0.0f)},
        C_ShapeDescriptor{IRRender::ShapeType::SPHERE, vec4(4, 4, 4, 0), Color{220, 140, 80, 255}}
    );

    // North-east: voxel-pool cube (per-voxel pick proves trixel readback works)
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
        ivec3 half{
            static_cast<int>(kSphereRadius) + 1,
            static_cast<int>(kSphereRadius) + 1,
            static_cast<int>(kSphereRadius) + 1
        };
        ivec3 size = half * 2 + ivec3(1);
        EntityId e = IREntity::createEntity(
            C_Position3D{vec3(0.0f, kRingRadius, 0.0f)},
            C_VoxelSetNew{size, Color{180, 100, 220, 255}, true}
        );
        auto &vs = IREntity::getComponent<C_VoxelSetNew>(e);
        carveSphere(vs, kSphereRadius);
    }

    // Point light between the shapes so lighting shows depth on all four.
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, -4.0f)},
        C_LocalTransform{vec3(0.0f, 0.0f, -4.0f)},
        C_LightSource{
            LightType::EMISSIVE,
            Color{200, 220, 255, 255},
            1.5f,
            static_cast<uint8_t>(20)
        }
    );

    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});

    IRRender::setSunDirection(kSunDirNormal);
}
