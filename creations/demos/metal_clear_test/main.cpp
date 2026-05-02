#include <irreden/ir_engine.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

namespace {

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {1.0f, vec2(0, 0),  "metal_clear_zoom1"},
    {2.0f, vec2(0, 0),  "metal_clear_zoom2"},
    {4.0f, vec2(4, 4),  "metal_clear_zoom4_offset"},
};

} // namespace

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: metal_clear_test");

    int autoWarmupFrames = 0;
    IRVideo::parseAutoScreenshotArgv(argc, argv, &autoWarmupFrames);

    IREngine::init(argv[0], "config.lua");

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>()}
    );

    std::list<IRSystem::SystemId> renderPipeline = {
        IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
    };

    if (autoWarmupFrames > 0) {
        IRVideo::AutoScreenshotConfig cfg{};
        cfg.warmupFrames_ = autoWarmupFrames;
        cfg.settleFrames_ = 3;
        cfg.shots_ = kShots;
        cfg.numShots_ = sizeof(kShots) / sizeof(kShots[0]);
        renderPipeline.push_back(IRVideo::createAutoScreenshotSystem(cfg));
    }

    IRSystem::registerPipeline(IRTime::Events::RENDER, renderPipeline);

    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );

    IREngine::gameLoop();
    return 0;
}
