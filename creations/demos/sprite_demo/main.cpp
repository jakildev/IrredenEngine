#include "lua_bindings.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_video.hpp>

#include <list>

// SYSTEMS
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_sprites_to_screen.hpp>
#include <irreden/render/systems/system_sprite_animation_advance.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

// COMMANDS
#include <irreden/common/command_suite_capture.hpp>

#include <cstring>

using namespace IRMath;

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0.0f, 0.0f), "layout_zoom1"},
    {2.0f, vec2(0.0f, 0.0f), "layout_zoom2"},
};

int g_autoWarmupFrames = 0;

} // namespace

void initSystems();
void initCommands();

int main(int argc, char **argv) {
    IRVideo::parseAutoScreenshotArgv(argc, argv, &g_autoWarmupFrames);

    IR_LOG_INFO("Starting creation: sprite_demo");
    IRSpriteDemoCreation::registerLuaBindings();
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    IREngine::runScript("main.lua");
    IREngine::gameLoop();

    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::SPRITE_ANIMATION_ADVANCE>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
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
            IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
            IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
            IRSystem::createSystem<IRSystem::SPRITE_TO_SCREEN>(),
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

void initCommands() {
    IRPrefab::Camera::registerStandardKeyboardCommands();
    IRCommand::registerCaptureCommands();
}
