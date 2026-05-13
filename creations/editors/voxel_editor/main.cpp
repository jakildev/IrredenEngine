#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

// Components
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_camera_yaw.hpp>
#include <irreden/render/components/component_voxel_selection.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>

// Gizmo primitives (T-152, F-0.5 Phase 1)
#include <irreden/render/gizmo.hpp>

// Systems
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_gizmo_screen_space_size.hpp>
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
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_gizmo_hover.hpp>
#include <irreden/render/systems/system_gizmo_drag.hpp>
#include <irreden/render/systems/system_voxel_picking.hpp>

// Camera prefab namespace (Z-yaw API)
#include <irreden/render/camera.hpp>

// Command suites
#include <irreden/common/command_suite_camera.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace {

// Radians per screen-pixel of horizontal mouse movement during right-drag.
constexpr float kRotationSensitivity = 0.004f;

struct ScrollZoomParams {
    EntityId cameraEntity_ = kNullEntity;
    int scrollDelta_ = 0;
};

struct RotateParams {
    bool firstRotFrame_ = true;
    float prevMouseX_ = 0.0f;
};

} // namespace

void initSystems();
void initCommands();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: voxel_editor");
    IR_LOG_INFO("  Right-drag: rotate model view (Z-yaw turntable)");
    IR_LOG_INFO("  Middle-drag: pan camera");
    IR_LOG_INFO("  Scroll: zoom in/out");
    IR_LOG_INFO("  Q/E: snap-rotate 90 deg CCW/CW");
    IR_LOG_INFO("  Space: re-center + reset yaw");
    IR_LOG_INFO("  Left-click: pick voxel (click empty to clear)");
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    initEntities();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    // Scroll zoom iterates C_MouseScroll entities, which are ephemeral (C_Lifetime{1}).
    // Register in INPUT so they are still alive; LIFETIME in UPDATE destroys them.
    auto scrollParams = std::make_unique<ScrollZoomParams>();
    auto *sp = scrollParams.get();
    auto scrollZoomSystem = IRSystem::createSystem<C_MouseScroll>(
        "EditorScrollZoom",
        [sp](C_MouseScroll &scroll) {
            if (scroll.yoffset_ > 0.0)
                ++sp->scrollDelta_;
            else if (scroll.yoffset_ < 0.0)
                --sp->scrollDelta_;
        },
        [sp]() { sp->cameraEntity_ = IREntity::getEntity("camera"); },
        [sp]() {
            if (sp->scrollDelta_ != 0) {
                auto &zoom = IREntity::getComponent<C_ZoomLevel>(sp->cameraEntity_);
                for (int i = 0; i < sp->scrollDelta_; ++i)
                    zoom.zoomIn();
                for (int i = 0; i > sp->scrollDelta_; --i)
                    zoom.zoomOut();
                sp->scrollDelta_ = 0;
            }
        }
    );
    IRSystem::setSystemParams(scrollZoomSystem, std::move(scrollParams));

    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {// Rescale gizmo handles to constant pixel size BEFORE the
         // global-position propagation system reads `C_Position3D`, so
         // the new local positions reach the SHAPES_TO_TRIXEL pass in
         // the same frame.
         IRSystem::createSystem<IRSystem::GIZMO_SCREEN_SPACE_SIZE>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()
        }
    );
    // GIZMO_HOVER reads the previous frame's entity-id GPU readback;
    // GIZMO_DRAG runs immediately after so press detection sees a fresh
    // hover flag in the same INPUT pipeline pass.
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         scrollZoomSystem,
         IRSystem::createSystem<IRSystem::GIZMO_HOVER>(),
         IRSystem::createSystem<IRSystem::GIZMO_DRAG>()}
    );

    // Right-click drag rotates the camera Z-yaw (turntable), which in the
    // isometric engine is equivalent to rotating the entity being edited.
    auto rotParams = std::make_unique<RotateParams>();
    auto *rp = rotParams.get();
    auto rotateSystem = IRSystem::createSystem<C_CameraYaw>(
        "EditorViewportRotate",
        [](C_CameraYaw &) {},
        [rp]() {
            bool rightPressed =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::PRESSED);
            bool rightHeld =
                IRInput::checkKeyMouseButton(IRInput::kMouseButtonRight, IRInput::HELD);

            if (rightPressed) {
                rp->firstRotFrame_ = true;
            }

            if (rightHeld) {
                vec2 mouse = IRInput::getMousePositionScreen();
                if (!rp->firstRotFrame_) {
                    float deltaX = mouse.x - rp->prevMouseX_;
                    IRPrefab::Camera::rotateYaw(deltaX * kRotationSensitivity);
                }
                rp->prevMouseX_ = mouse.x;
                rp->firstRotFrame_ = false;
            }
        }
    );
    IRSystem::setSystemParams(rotateSystem, std::move(rotParams));

    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {
            rotateSystem,
            IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
            IRSystem::createSystem<IRSystem::VOXEL_PICKING>(),
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
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
        }
    );
}

void initCommands() {
    IRCommand::registerCameraCommands();

    // Q: snap yaw to nearest 90 degrees counterclockwise
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonQ,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q - 1) * IRMath::kHalfPi);
        }
    );

    // E: snap yaw to nearest 90 degrees clockwise
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonE,
        []() {
            auto q = static_cast<int>(IRMath::round(IRPrefab::Camera::getYaw() / IRMath::kHalfPi));
            IRPrefab::Camera::setYaw(static_cast<float>(q + 1) * IRMath::kHalfPi);
        }
    );

    // Space: re-center camera pan and reset yaw to default front view
    IRCommand::createCommand(
        IRInput::InputTypes::KEY_MOUSE,
        IRInput::ButtonStatuses::PRESSED,
        IRInput::KeyMouseButtons::kKeyButtonSpace,
        []() {
            IRRender::setCameraPosition2DIso(vec2(0.0f, 0.0f));
            IRPrefab::Camera::setYaw(0.0f);
        }
    );
}

void initEntities() {
    // Dockspace placeholder — F-0.1 (trixel UI primitives) and F-0.2 (layout system)
    // will add the widget-based dockspace here.

    // Reference grid: a flat ground plane with axis markers at the origin.
    // +Z is downward in this iso convention; the work plane sits at z=0 and
    // the floor reference plane sits at z=2 below it.
    constexpr float kFloorZ = 2.0f;

    // Ground plane
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, kFloorZ)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(40.0f, 40.0f, 1.0f, 0.0f),
            Color{55, 60, 75, 255}
        }
    );

    // X-axis indicator (red, extends along X)
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(16.0f, 0.5f, 0.5f, 0.0f),
            Color{180, 60, 60, 255}
        }
    );

    // Y-axis indicator (green, extends along Y)
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(0.5f, 16.0f, 0.5f, 0.0f),
            Color{60, 180, 60, 255}
        }
    );

    // Origin marker
    IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(1.5f, 1.5f, 1.5f, 0.0f),
            Color{220, 220, 240, 255}
        }
    );

    // F-0.5 Phase 1: place one of each gizmo primitive at fixed offsets
    // around the origin so the geometry is inspectable. Hover, drag, and
    // screen-space sizing are deferred to follow-up tasks; see T-152 plan.
    {
        EntityId translateGizmo = IRPrefab::Gizmo::createTranslateGizmo();
        IREntity::getComponent<C_Position3D>(translateGizmo).pos_ = vec3(-12.0f, 12.0f, -3.0f);

        EntityId rotateGizmo = IRPrefab::Gizmo::createRotateGizmo();
        IREntity::getComponent<C_Position3D>(rotateGizmo).pos_ = vec3(12.0f, 12.0f, -3.0f);

        EntityId scaleGizmo = IRPrefab::Gizmo::createScaleGizmo();
        IREntity::getComponent<C_Position3D>(scaleGizmo).pos_ = vec3(-12.0f, -12.0f, -3.0f);

        EntityId jointMarker = IRPrefab::Gizmo::createJointMarker();
        IREntity::getComponent<C_Position3D>(jointMarker).pos_ = vec3(8.0f, -12.0f, -3.0f);

        EntityId bindPointMarker = IRPrefab::Gizmo::createBindPointMarker();
        IREntity::getComponent<C_Position3D>(bindPointMarker).pos_ = vec3(12.0f, -12.0f, -3.0f);

        EntityId ikMarker = IRPrefab::Gizmo::createIKMarker();
        IREntity::getComponent<C_Position3D>(ikMarker).pos_ = vec3(16.0f, -12.0f, -3.0f);
    }

    // Selection-highlight entity: a slightly oversized box marker
    // positioned at the picked voxel. Created hidden — system_voxel_picking
    // toggles SHAPE_FLAG_VISIBLE on/off and rewrites position on each click.
    // The 1.4× sizing reads as a halo around the underlying voxel rather
    // than z-fighting with it.
    EntityId highlight = IREntity::createEntity(
        C_Position3D{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(1.4f, 1.4f, 1.4f, 0.0f),
            Color{255, 220, 80, 255}
        },
        C_VoxelSelection{},
        C_VoxelSelectionHighlight{}
    );
    auto &highlightShape = IREntity::getComponent<C_ShapeDescriptor>(highlight);
    highlightShape.flags_ &= ~IRRender::SHAPE_FLAG_VISIBLE;

    // Canvas setup
    EntityId mainCanvas = IRRender::getActiveCanvasEntity();
    const ivec2 canvasSize = IREntity::getComponent<C_TriangleCanvasTextures>(mainCanvas).size_;
    IREntity::setComponent(mainCanvas, C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(mainCanvas, C_CanvasAOTexture{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasSunShadow{canvasSize});
    IREntity::setComponent(mainCanvas, C_CanvasLightVolume{});

    IRRender::setSunDirection(vec3(0.35f, 0.85f, -0.4f));
}
