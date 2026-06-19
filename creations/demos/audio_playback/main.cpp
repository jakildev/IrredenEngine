// audio_playback demo (engine #1813) — exercises the file-playback substrate
// (IRAudio.playSound / playMusic / bus + master volume / fade) end-to-end from
// Lua. The C++ side registers `bindLuaDrivenEcs()` (which wires the IRAudio Lua
// table), composes a minimal voxel render pipeline so the run has a frame to
// capture, spawns one voxel cube, then runs `scripts/audio_demo.lua`. The Lua
// script does all the audio work. `--auto-screenshot N` runs N warmup frames,
// captures, and closes the window — the headless run+exit path.

#include <irreden/ir_engine.hpp>
#include <irreden/ir_video.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <irreden/render/camera_controls.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0), 0.0f, "audio_playback_zoom1"},
};

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    auto renderPipeline = IRPrefab::Camera::standardControlSystems();
    renderPipeline.insert(
        renderPipeline.end(),
        {IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
         IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
         IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
    );
    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);
}

void initEntities() {
    IREntity::createEntity(
        C_LocalTransform{vec3(0.0f)},
        C_VoxelSetNew{ivec3(8, 8, 8), Color{120, 180, 220, 255}}
    );
}

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: audio_playback");

    int autoWarmupFrames = 0;
    IRVideo::parseAutoScreenshotArgv(argc, argv, &autoWarmupFrames);

    // Wire the Lua-driven ECS surface, which now also exposes the IRAudio
    // file-playback table consumed by audio_demo.lua.
    IREngine::registerLuaBindings([](IRScript::LuaScript &script) {
        script.bindLuaDrivenEcs();
    });

    IREngine::init(argv[0], "config.lua");
    initSystems();
    initEntities();
    IREngine::runScript("audio_demo.lua");

    if (autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        IRSystem::appendToPipeline(
            IRTime::Events::RENDER,
            IRVideo::createAutoScreenshotSystem(cfg)
        );
    }

    IREngine::gameLoop();
    return 0;
}
