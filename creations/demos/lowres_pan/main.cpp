#include <irreden/ir_engine.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/voxel/systems/system_rebuild_grid_voxels.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

#include <irreden/common/command_suite_capture.hpp>
#include <irreden/render/camera_controls.hpp>

// lowres_pan exercises the sub-pixel-smooth camera path at a low game
// resolution. The shot list steps the camera in fractional iso increments;
// at zoom=1 / scaleFactor=4, each 0.05 iso step is 0.4 screen pixels, well
// below a single game pixel. A working anti-vibration decomposition slides
// the upscale blit smoothly between game-pixel boundaries — a broken one
// either snaps in whole-game-pixel jumps (no upscale residual) or vibrates
// by +/-1 screen pixel as floor() disagrees with itself across the two
// stages. The shot diffs make either failure visible to a human reviewer
// without having to drive the camera by hand. See
// `IRMath::cameraSubPixelOffsets` for the math invariant the test exercises.

using namespace IRComponents;
using namespace IRMath;

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0.00f, 0.0f), "pan_x_00"},
    {1.0f, vec2(0.05f, 0.0f), "pan_x_05"},
    {1.0f, vec2(0.10f, 0.0f), "pan_x_10"},
    {1.0f, vec2(0.15f, 0.0f), "pan_x_15"},
    {1.0f, vec2(0.20f, 0.0f), "pan_x_20"},
    {1.0f, vec2(0.25f, 0.0f), "pan_x_25"},
    {1.0f, vec2(0.30f, 0.0f), "pan_x_30"},
    {1.0f, vec2(0.35f, 0.0f), "pan_x_35"},
};

int g_autoWarmupFrames = 0;

} // namespace

void initEntities();
void initSystems();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);

    IR_LOG_INFO("Starting creation: lowres_pan");
    IREngine::init(argv[0]);
    initSystems();
    initEntities();
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
    IREngine::gameLoop();
    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::REBUILD_GRID_VOXELS>()}
    );
    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {
            IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
            IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
        }
    );

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

void initEntities() {
    // Two SDF shapes spaced so the camera pan range moves them visibly across
    // the 320x180 framebuffer. A box and a sphere — different silhouettes
    // make sub-pixel motion easier to spot at the upscale boundary.
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::BOX,
            vec4(7.0f, 7.0f, 7.0f, 0.0f),
            Color{220, 180, 100, 255}
        }
    );
    IREntity::createEntity(
        C_LocalTransform{vec3(12.0f, 0.0f, 0.0f)},
        C_ShapeDescriptor{
            IRRender::ShapeType::SPHERE,
            vec4(4.0f, 4.0f, 4.0f, 0.0f),
            Color{100, 200, 220, 255}
        }
    );
}
